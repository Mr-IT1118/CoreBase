"""CoreBase CPU collector. Run beside collector.py and database.py.

python cpu_collector.py --brand amd
python cpu_collector.py --brand intel --limit-intel 10 --save
python cpu_collector.py --save

AMD reads the entire public data-json table. Intel reads ARK series directories,
then visits new SKU pages. --refresh-intel also revisits existing Intel SKUs.
Saving commits AMD once and Intel by series, so completed series survive a restart.
The database supplies timestamps. Unknown specs and fractional values that cannot
fit INTEGER columns are None. Vendor codenames are not microarchitectures.
Intel e_cores uses ARK's Efficient-cores field, which excludes separately listed
Low Power Efficient-cores. cores always uses the vendor's total.
"""

import argparse
import json
import logging
import re
import sqlite3
from contextlib import closing
from dataclasses import dataclass, field
from decimal import Decimal
from urllib.parse import urljoin, urlsplit, urlunsplit

import database
from collector import (AMD_DATA_JS, AMD_DATA_SELECTOR, amd_expected_total, amd_field_values,
                       amd_technology_architecture, clean, collector_page, navigate)

LOG = logging.getLogger("corebase.cpu")
AMD_URL = "https://www.amd.com/en/products/specifications/processors.html"
INTEL_INDEX = "https://www.intel.com/content/www/us/en/ark.html"
SKU_PATH = re.compile(r"/products/sku/(\d+)/[^/]+/specifications\.html/?$", re.I)
SERIES_PATH = re.compile(r"/ark/products/series/\d+/[^/]+\.html$", re.I)
FORMS = {
    "desktop": "Desktop", "desktops": "Desktop", "1l desktops": "Desktop",
    "mobile": "Mobile", "laptops": "Mobile", "notebook": "Mobile", "handheld": "Mobile",
    "workstation": "Workstation", "workstations": "Workstation", "mobile workstations": "Mobile",
}
UNSUPPORTED = {"server", "embedded", "communications", "networking"}

# Only links inside the product table are SKU candidates. Hidden rows are included.
DIRECTORY_JS = """() => {
    const table = document.querySelector('#product-table');
    const rows = Array.from(table.tBodies).flatMap(body => Array.from(body.rows));
    return rows.map(row => {
        const a = row.querySelector('a[href*="/sku/"]');
        return {name: a?.textContent.trim(), url: a?.href,
                segment: row.getAttribute('data-filter') || '',
                id: row.getAttribute('data-product-id')};
    });
}"""
DETAIL_JS = """() => Array.from(document.querySelectorAll('table tr'))
    .filter(row => row.cells.length === 2)
    .map(row => Array.from(row.cells, cell => cell.innerText.trim()))"""


@dataclass
class Batch:
    rows: list = field(default_factory=list)
    errors: list = field(default_factory=list)
    cached: int = 0


def official_url(url, host, base):
    if not url:
        return None
    parts = urlsplit(urljoin(base, url))
    if parts.scheme != "https" or parts.hostname not in {host, "www." + host}:
        return None
    return urlunsplit(("https", "www." + host, parts.path, "", ""))


def intel_sku(url):
    url = official_url(url, "intel.com", INTEL_INDEX)
    match = SKU_PATH.search(urlsplit(url).path) if url else None
    return match[1] if match else None


def launch_year(value):
    year = re.search(r"\b(?:19|20)\d{2}\b", value or "")
    if year:
        return int(year[0])
    # Official sources also use Q4'24 and US month/day/yy.
    short = re.fullmatch(r"(?:Q[1-4]\s*['’]\s*|\d{1,2}/\d{1,2}/)(\d{2})", value or "", re.I)
    if short:
        yy = int(short[1])
        return (1900 if yy >= 70 else 2000) + yy
    return None


def quantity(value, unit="", *, integer=False, zero=False):
    """Only accept one unambiguous number with an explicit, compatible unit."""
    units = {"": {"": 1}, "GHz": database.CLOCK_GHZ,
             "MB": database.CACHE_MB, "W": database.POWER_W}[unit]
    match = re.fullmatch(r"(?:up to\s+)?(\d+(?:\.\d+)?)\s*([a-z]*)", value or "", re.I)
    if not match:
        return None
    scale = next((factor for label, factor in units.items() if label.lower() == match[2].lower()), None)
    if scale is None:
        return None
    number = Decimal(match[1]) * scale
    if (number == 0 and not zero) or (integer and number != number.to_integral_value()):
        return None
    return int(number) if integer else float(number)


def cpu_record(brand, model, form_factor, source_url, **specs):
    if not model or not form_factor:
        raise ValueError("Missing product name or supported form factor")
    cores = specs.get("cores")
    if cores is not None and cores < (specs.get("p_cores") or 0) + (specs.get("e_cores") or 0):
        raise ValueError("P/E core counts exceed total core count")
    if cores is not None and specs.get("threads") is not None and specs["threads"] < cores:
        raise ValueError("Thread count is smaller than core count")
    row = database._normalize("CPU", dict(brand=brand, model=model, form_factor=form_factor,
                                          source_url=source_url, **specs))
    if row.keys() - set(database.CPU_FIELDS):
        raise ValueError(f"Unknown CPU fields: {row.keys() - set(database.CPU_FIELDS)}")
    return {key: row.get(key) for key in database.CPU_FIELDS}


def amd_record(item):
    elements = item.get("elements") if isinstance(item, dict) else None
    if not isinstance(elements, dict) or not {"name", "numOfCpuCores"} <= elements.keys():
        raise ValueError("Missing AMD processor specification fields")

    def values(key):
        return amd_field_values(elements.get(key, {}))

    def value(key):
        return ", ".join(values(key)) or None

    names = values("name")
    if len(names) != 1:
        raise ValueError("Missing or ambiguous AMD model name")
    link = official_url((item.get("productPages") or {}).get("en"), "amd.com", AMD_URL)
    forms = [FORMS[label.casefold()] for label in values("formFactor") if label.casefold() in FORMS]
    # The schema holds one platform. Prefer the official product-page category,
    # then the first supported platform in AMD's ordered formFactor list.
    category = re.search(r"/processors/(laptop|desktops?|workstations?)/", link or "")
    form = forms[0] if forms else None
    if category:
        form = {"laptop": "Mobile", "desktop": "Desktop", "workstation": "Workstation"}[
            category[1].rstrip("s")]
    graphics = value("graphicsModel")
    if graphics and graphics.casefold() in {"discrete graphics card required", "no", "none", "n/a"}:
        graphics = None
    return cpu_record(
        "AMD", names[0], form, link or AMD_URL,
        series=value("series"), architecture=(value("architecture") or value("cpuCoreArchitecture")
                                              or amd_technology_architecture(elements)),
        cores=quantity(value("numOfCpuCores"), integer=True),
        threads=quantity(value("numOfThreads"), integer=True),
        base_clock_ghz=quantity(value("baseClock"), "GHz"),
        boost_clock_ghz=quantity(value("maxBoostClock"), "GHz"),
        l2_cache_mb=quantity(value("l2Cache"), "MB", zero=True),
        l3_cache_mb=quantity(value("l3Cache"), "MB", integer=True, zero=True),
        socket=value("cpuSocket"), memory_support=value("systemMemoryType"),
        pcie_version=value("pciExpressVersion"), tdp_w=quantity(value("defaultTdp"), "W", integer=True),
        igpu_model=graphics, release_year=launch_year(value("launchDate")),
    )


def parse_amd_snapshot(payload, expected_total=None):
    items = payload.get("items") if isinstance(payload, dict) else None
    if not isinstance(items, list) or not items:
        raise ValueError("Empty or unrecognized AMD processor snapshot")
    if expected_total is not None and expected_total != len(items):
        raise ValueError(f"AMD snapshot has {len(items)} products, table reports {expected_total}")
    rows, conflicts = {}, set()
    batch = Batch()
    for index, item in enumerate(items, 1):
        try:
            row = amd_record(item)
        except (ValueError, TypeError, AttributeError) as exc:
            label = item.get("title") if isinstance(item, dict) else None
            batch.errors.append(f"AMD item {index} ({label or 'untitled'}): {exc}")
            continue
        key = row["product_id"]
        if key in rows and rows[key] != row:
            conflicts.add(key)
        else:
            rows[key] = row
    for key in sorted(conflicts):
        del rows[key]
        batch.errors.append(f"Conflicting AMD records for {key}")
    batch.rows = list(rows.values())
    if not batch.rows:
        raise ValueError("No valid AMD CPU records. " + " | ".join(batch.errors[:3]))
    return batch


def collect_amd(page):
    navigate(page, AMD_URL, "amd.com")
    page.wait_for_function("""selector => {
        const value = document.querySelector(selector)?.getAttribute('data-json');
        if (!value) return false;
        try { return Array.isArray(JSON.parse(value).items); }
        catch { return false; }
    }""", arg=AMD_DATA_SELECTOR, timeout=30000)
    snapshot = page.evaluate(AMD_DATA_JS)
    return parse_amd_snapshot(snapshot["payload"], amd_expected_total(snapshot["summary"]))


def intel_record(name, url, pairs):
    data = {clean(key).replace("‡", "").replace("†", "").strip().casefold(): clean(value)
            for key, value in pairs if value}

    def get(*labels):
        return next((data[k] for k in labels if data.get(k)), None)

    segment = (get("vertical segment") or "").casefold()
    if segment in UNSUPPORTED:
        return None
    if not get("processor number") and not get("total cores", "# of cores"):
        raise ValueError("Expected Intel CPU specifications")
    # Keep catalog model qualifiers, e.g. 'Plus', that distinguish SKUs.
    model = clean(name)
    if re.match(r"^Intel (?:Core|Xeon|Atom|Celeron|Pentium)\b", model, re.I):
        model = " ".join(re.sub(r"\bprocessor\b", "", model, flags=re.I).split())
    graphics = get("gpu name", "processor graphics")
    if ((graphics or "").casefold() in {"no", "none", "n/a"} or model.upper().endswith("F")
            or re.search(r"\d[A-Z]*F\s+Plus$", model, re.I)):
        graphics = None
    cache = get("cache") or ""
    l2 = get("total l2 cache", "l2 cache")
    l3 = get("total l3 cache", "l3 cache")
    match = re.fullmatch(r"(\d+(?:\.\d+)?\s*(?:KB|MB))\s+(Intel Smart Cache|L[23] Cache)", cache, re.I)
    if match:
        if match[2].casefold() == "l2 cache":
            l2 = l2 or match[1]
        else:
            l3 = l3 or match[1]
    return cpu_record(
        "Intel", model, FORMS.get(segment), url,
        series=get("product collection"), architecture=get("microarchitecture", "core microarchitecture"),
        cores=quantity(get("total cores", "# of cores"), integer=True),
        p_cores=quantity(get("# of performance-cores"), integer=True, zero=True),
        e_cores=quantity(get("# of efficient-cores"), integer=True, zero=True),
        threads=quantity(get("total threads", "# of threads"), integer=True),
        base_clock_ghz=quantity(get("processor base frequency", "performance-core base frequency"), "GHz"),
        boost_clock_ghz=quantity(get("max turbo frequency"), "GHz"),
        l2_cache_mb=quantity(l2, "MB", zero=True), l3_cache_mb=quantity(l3, "MB", integer=True, zero=True),
        socket=get("sockets supported"), memory_support=get("memory types"),
        pcie_version=get("pci express revision"), tdp_w=quantity(get("processor base power", "tdp"), "W", integer=True),
        max_turbo_power_w=quantity(get("maximum turbo power"), "W", integer=True),
        igpu_model=graphics, release_year=launch_year(get("launch date")),
    )


def intel_inventory():
    """Use only committed CPU rows as checkpoints. Preview never initializes the DB."""
    known, owners, models = set(), {}, {}
    path = database.DB_PATH
    if path.exists():
        with closing(sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True)) as db:
            if db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='cpus'").fetchone():
                for key, model, url, cores in db.execute(
                    "SELECT product_id, model, source_url, cores FROM cpus WHERE brand='Intel'"
                ):
                    sku = intel_sku(url)
                    if sku:
                        owners[key] = sku
                        models[sku] = (key, model)
                        if cores:
                            known.add(sku)
    return known, owners, models


def intel_series(page):
    navigate(page, INTEL_INDEX, "intel.com")
    links = page.locator('a[href*="/ark/products/series/"]')
    links.first.wait_for(state="attached", timeout=30000)
    found = {}
    for name, href in links.evaluate_all("nodes => nodes.map(a => [a.textContent.trim(), a.href])"):
        url = official_url(href, "intel.com", INTEL_INDEX)
        if url and SERIES_PATH.search(urlsplit(url).path) and re.search(r"\bprocessors?\b", clean(name), re.I):
            found[url] = clean(name)
    if not found:
        raise ValueError("Intel ARK index returned no processor series")
    return found


def intel_directory(page, url):
    navigate(page, url, "intel.com")
    page.locator('#product-table tbody tr a[href*="/sku/"]').first.wait_for(state="attached", timeout=30000)
    entries = page.evaluate(DIRECTORY_JS)
    if not entries:
        raise ValueError("Intel series returned an empty directory")
    return entries


def intel_detail(page, name, url):
    navigate(page, url, "intel.com")
    if intel_sku(page.url) != intel_sku(url):
        raise ValueError("Intel redirected to a different SKU")
    page.wait_for_function("""() => Array.from(document.querySelectorAll('table tr'))
        .some(r => r.cells.length === 2 && /^(Total Cores|# of Cores|Processor Number)$/i.test(r.cells[0].innerText.trim()))""",
                           timeout=30000)
    return intel_record(name, url, page.evaluate(DETAIL_JS))


def intel_batches(page, refresh=False, limit=None):
    known, owners, models = intel_inventory()
    visited = set()
    fetched = 0
    for series_url, series in intel_series(page).items():
        batch = Batch()
        try:
            entries = intel_directory(page, series_url)
        except Exception as exc:
            batch.errors.append(f"{series_url}: {exc}")
            yield batch
            continue
        for entry in entries:
            url = official_url(entry["url"], "intel.com", INTEL_INDEX)
            sku = intel_sku(url)
            if not sku or not entry["name"] or (entry.get("id") and str(entry["id"]) != sku):
                batch.errors.append(f"{series}: invalid product directory row ({entry.get('id')})")
                continue
            if (entry.get("segment") or "").strip().casefold() in UNSUPPORTED or sku in visited:
                continue
            visited.add(sku)
            if not refresh and sku in known:
                batch.cached += 1
                continue
            fetched += 1
            try:
                row = intel_detail(page, entry["name"], url)
                if row:
                    # A known SKU may acquire a different catalog display name.
                    # CPU primary keys depend on that name, so retain stored identity.
                    if sku in models:
                        row["product_id"], row["model"] = models[sku]
                    key = row["product_id"]
                    if owners.get(key, sku) != sku:
                        raise ValueError(f"{key} already belongs to a different Intel SKU ({owners[key]})")
                    owners[key] = sku
                    batch.rows.append(row)
            except Exception as exc:
                batch.errors.append(f"Intel SKU {sku} ({url}): {exc}")
            if limit is not None and fetched >= limit:
                break
        LOG.info("%s: %d collected, %d already stored, %d errors",
                 series, len(batch.rows), batch.cached, len(batch.errors))
        yield batch
        if limit is not None and fetched >= limit:
            break


def source_batches(playwright, brand, headless, refresh, limit):
    try:
        with collector_page(playwright, brand, headless) as page:
            if brand == "amd":
                yield collect_amd(page)
            else:
                yield from intel_batches(page, refresh=refresh, limit=limit)
    except Exception as exc:
        yield Batch(errors=[f"{brand}: {exc}"])


def collect_cpus(brands=None, headless=True, *, save=False, refresh_intel=False, limit_intel=None, report=None):
    """Return CPU_FIELDS dictionaries. An empty, successful incremental run returns []."""
    selected = ["amd", "intel"] if brands is None else list(dict.fromkeys(brands))
    if not selected or set(selected) - {"amd", "intel"}:
        raise ValueError("Select amd and/or intel")
    if limit_intel is not None and limit_intel < 1:
        raise ValueError("limit_intel must be positive")
    if report is None:
        report = {}
    try:
        from playwright.sync_api import sync_playwright
    except ImportError as exc:
        raise RuntimeError("Install the GPU collector's browser dependency with "
                           "'python -m pip install playwright' and 'python -m playwright install chromium'.") from exc

    result = []
    with sync_playwright() as p:
        for brand in selected:
            started = database.utc_now()
            errors, count, cached = [], 0, 0
            for batch in source_batches(p, brand, headless, refresh_intel, limit_intel):
                cached += batch.cached
                errors.extend(batch.errors)
                if save and batch.rows:
                    database.save_cpus(batch.rows, brand=brand, started_at=started,
                                       skipped=len(batch.errors), error_message="\n".join(batch.errors) or None)
                elif save and batch.errors:
                    database.record_sync_failure("CPU", brand, "\n".join(batch.errors), started, len(batch.errors))
                result.extend(batch.rows)
                count += len(batch.rows)
            if save and not count and not errors:
                database.save_cpus([], brand=brand, started_at=started)
            status = ("partial" if count or cached else "failed") if errors else "success"
            report[brand] = dict(status=status, records=count, cached=cached, errors=errors)
            LOG.info("%s: %s, %d collected, %d already stored, %d errors", brand, status, count, cached, len(errors))
            for error in errors:
                LOG.warning("%s", error)
    if all(report[brand]["status"] == "failed" for brand in selected):
        raise RuntimeError("CPU collection failed: " + " | ".join(report[brand]["errors"][0] for brand in selected))
    return result


def collect_and_save_cpus(brands=None, headless=True, **options):
    return collect_cpus(brands, headless, save=True, **options)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Collect official AMD and Intel CPUs. Default: preview.")
    parser.add_argument("--brand", type=str.lower, choices=("amd", "intel", "all"), default="all")
    parser.add_argument("--save", action="store_true", help="Upsert into database.py's cpus table")
    parser.add_argument("--refresh-intel", action="store_true", help="Revisit Intel SKUs already in cpus")
    parser.add_argument("--limit-intel", type=int, metavar="N", help="Visit at most N Intel detail pages this run")
    parser.add_argument("--show-browser", action="store_true")
    args = parser.parse_args(argv)
    if args.limit_intel is not None and args.limit_intel < 1:
        parser.error("--limit-intel must be positive")
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    report = {}
    try:
        rows = collect_cpus(None if args.brand == "all" else [args.brand], not args.show_browser,
                            save=args.save, refresh_intel=args.refresh_intel, limit_intel=args.limit_intel, report=report)
    except Exception as exc:
        LOG.error("%s", exc)
        return 1
    print(json.dumps(rows, indent=2, ensure_ascii=False))
    return 2 if any(info["status"] != "success" for info in report.values()) else 0


if __name__ == "__main__":
    raise SystemExit(main())
