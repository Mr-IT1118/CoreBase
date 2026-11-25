"""CoreBase official GPU collector. Requires: pip install playwright.

Keeps the existing collect_gpus() interface and five-column SQLite schema.
Only complete desktop GPU records are returned. No guessed specifications.
"""

import json
import logging
import re
import time
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlparse

LOG = logging.getLogger(__name__)
NVIDIA_URL = "https://www.nvidia.com/en-us/geforce/graphics-cards/compare/"
AMD_URL = "https://www.amd.com/en/products/specifications/graphics.html"
INTEL_URL = "https://www.intel.com/content/www/us/en/ark/products/series/227960/intel-arc-dedicated-graphics-family.html"

# Read the rendered tables, including blank cells (never shift column indexes).
# AMD header cells contain filter menus: read their button labels, not options.
TABLES_JS = r"""() => Array.from(document.querySelectorAll('table')).map(t =>
    Array.from(t.rows).map(r => Array.from(r.cells).map(c => ({
        text: (c.querySelector('select')
            ? (c.querySelector('button')?.textContent || '')
            : c.textContent).replace(/\s+/g, ' ').trim(),
        link: c.querySelector('a[href]')?.href || null
    }))))"""


def clean(value):
    return re.sub(r"\s+", " ", value.replace("\u00ae", "").replace("\u2122", "")).strip()


def slug(value):
    return re.sub(r"[^a-z0-9]+", "-", clean(value).lower()).strip("-")


def memory_variants(value):
    """Preserve capacity and type, e.g. 12 GB GDDR6 / 12 GB GDDR6X."""
    parts = re.split(r"\s*/\s*|\s+or\s+", clean(value), flags=re.I)
    all_types = set(re.findall(r"GDDR\dX?|HBM\d?|DDR\d", value, re.I))
    variants = []
    for part in parts:
        capacity = re.search(r"(?<![\d.])(\d+)\s*GB\b", part, re.I)
        kind = re.search(r"GDDR\dX?|HBM\d?|DDR\d", part, re.I)
        if not capacity:
            raise ValueError(f"Unrecognized memory capacity: {value!r}")
        memory_type = kind.group().upper() if kind else (
            next(iter(all_types)).upper() if len(all_types) == 1 else None
        )
        if not memory_type:
            raise ValueError(f"Ambiguous memory type: {value!r}")
        size = int(capacity.group(1))
        if not 0 < size <= 1024:
            raise ValueError(f"Invalid memory capacity: {value!r}")
        variants.append((size, memory_type))
    return variants


def records(vendor, model, memory, bus, source_url, identity=None):
    variants = memory_variants(memory)
    widths = re.findall(r"(?<!\d)(\d+)\s*[-\u2013\u2014]?\s*bits?\b", bus, re.I)
    if len(widths) not in (1, len(variants)):
        raise ValueError(f"Missing or ambiguous memory interface: {bus!r}")
    if len(widths) == 1:
        widths *= len(variants)
    model = clean(model)
    if vendor == "nvidia" and not model.startswith("NVIDIA"):
        model = "NVIDIA " + model
    output = []
    for (size, kind), width in zip(variants, widths):
        width = int(width)
        if not 0 < width <= 8192:
            raise ValueError(f"Invalid memory interface: {width}")
        # Capacity/type form part of the key: variants must not overwrite each other.
        product_id = f"{vendor}-{slug(identity or model)}-{size}gb-{kind.lower()}"
        output.append({
            "product_id": product_id,
            "model": f"{model} [{size} GB {kind}]",
            "vram_gb": size,
            "bus_bits": width,
            "source_url": source_url,
        })
    return output


def parse_nvidia(tables):
    output, skipped = [], []
    for table in tables:
        rows = [[cell["text"] for cell in row] for row in table]
        memory = next((r for r in rows if r and r[0] == "Standard Memory Config"), None)
        if memory is None:
            continue
        header = next((r for r in rows if sum("GeForce" in c for c in r) >= 1), None)
        bus = next((r for r in rows if r and r[0] == "Memory Interface Width"), None)
        if not header or not bus or not (len(header) == len(memory) == len(bus)):
            raise ValueError("NVIDIA table columns changed; refused to guess alignment")
        for index in range(1, len(header)):
            try:
                output.extend(records("nvidia", header[index], memory[index], bus[index], NVIDIA_URL))
            except ValueError as exc:
                skipped.append(f"{header[index]}: {exc}")
    if not output:
        raise ValueError("No complete NVIDIA specification rows found")
    return output, skipped


def parse_amd(tables):
    output, skipped, found = [], [], False
    for table in tables:
        header_index = next((i for i, row in enumerate(table)
                             if {"Name", "Max Memory Size", "Memory Interface"}
                             <= {cell["text"] for cell in row}), None)
        if header_index is None:
            continue
        found = True
        headings = [cell["text"] for cell in table[header_index]]
        for row in table[header_index + 1:]:
            if len(row) != len(headings):
                raise ValueError("AMD table columns changed; refused to guess alignment")
            data = dict(zip(headings, [cell["text"] for cell in row]))
            # AMD calls some desktop add-in cards 'Component'. Exclude notebooks.
            if data.get("Board Type") not in ("Desktop", "Component"):
                continue
            try:
                output.extend(records(
                    "amd", data["Name"],
                    data["Max Memory Size"] + " " + data.get("Memory Type", ""),
                    data["Memory Interface"], AMD_URL,
                ))
            except ValueError as exc:
                skipped.append(f"{data['Name']}: {exc}")
    if not found:
        raise ValueError("AMD specifications table was not found")
    return output, skipped


def parse_intel(tables, source_url):
    data = {row[0]["text"]: row[1]["text"]
            for table in tables for row in table if len(row) == 2}
    if data.get("Vertical Segment") != "Desktop":
        raise ValueError("Intel product is not confirmed as a desktop GPU")
    model = data.get("Model Number", "")
    if not re.fullmatch(r"[A-Z]\d{3}", model):
        raise ValueError("Intel desktop model number was not found")
    sku = re.search(r"/sku/(\d+)/", source_url)
    if not sku:
        raise ValueError("Intel SKU was not found in the source URL")
    # B-series combines these fields; A-series lists capacity and type separately.
    memory = data.get("Memory") or (data.get("Memory Size", "") + " " + data.get("Memory Type", ""))
    return records("intel", f"Intel Arc {model}", memory,
                   data.get("Graphics Memory Interface", ""), source_url,
                   identity="sku-" + sku.group(1))


def navigate(page, url, domain):
    time.sleep(2)  # Low request rate; no parallel scraping or automatic retry loops.
    LOG.info("Opening %s", url)
    response = page.goto(url, wait_until="domcontentloaded", timeout=45000)
    if response and response.status >= 400:
        raise RuntimeError(f"HTTP {response.status}: {url}")
    host = (urlparse(page.url).hostname or "").lower()
    if host != domain and not host.endswith("." + domain):
        raise RuntimeError(f"Unexpected redirect away from {domain}")


def wait_for_tables(page, label):
    page.wait_for_function(
        "label => Array.from(document.querySelectorAll('table')).some(t => t.textContent.includes(label))",
        arg=label, timeout=30000,
    )
    return page.evaluate(TABLES_JS)


def collect_nvidia(page):
    navigate(page, NVIDIA_URL, "nvidia.com")
    return parse_nvidia(wait_for_tables(page, "Standard Memory Config"))


def collect_amd(page):
    navigate(page, AMD_URL, "amd.com")
    page.locator("table tbody tr td").first.wait_for(state="attached", timeout=30000)
    output, skipped, seen_pages = [], [], set()
    for _ in range(20):
        tables = wait_for_tables(page, "Max Memory Size")
        signature = json.dumps(tables, sort_keys=True)
        if signature in seen_pages:
            raise RuntimeError("AMD pagination repeated a page; collection is incomplete")
        seen_pages.add(signature)
        rows, warnings = parse_amd(tables)
        output.extend(rows)
        skipped.extend(warnings)
        next_button = page.get_by_role("button", name="Next", exact=True)
        if next_button.count() != 1:
            raise RuntimeError("AMD Next button changed; collection is incomplete")
        if not next_button.is_enabled():
            if not output:
                raise ValueError("No complete AMD desktop specifications found")
            return output, skipped
        previous = page.locator("table tbody tr td").first.text_content()
        time.sleep(2)
        next_button.click(timeout=10000)
        page.wait_for_function(
            "previous => { const c = document.querySelector('table tbody tr td'); return c && c.textContent !== previous; }",
            arg=previous, timeout=15000,
        )
    raise RuntimeError("AMD reached the 20-page safety limit; collection is incomplete")


def collect_intel(page):
    navigate(page, INTEL_URL, "intel.com")
    page.locator('a[href*="/products/sku/"][href$="/specifications.html"]').first.wait_for(
        state="attached", timeout=30000,
    )
    links = page.locator('a[href*="/products/sku/"][href$="/specifications.html"]').evaluate_all(
        "nodes => nodes.map(a => ({name: a.textContent, url: a.href}))"
    )
    urls = list(dict.fromkeys(link["url"] for link in links
                            if re.fullmatch(r"Intel Arc [A-Z]\d{3} Graphics", clean(link["name"]))
                            and urlparse(link["url"]).hostname == "www.intel.com"))
    if not urls or len(urls) > 50:
        raise ValueError("Intel desktop product list is empty or unexpectedly large")
    output, skipped = [], []
    for url in urls:
        try:
            navigate(page, url, "intel.com")
            output.extend(parse_intel(wait_for_tables(page, "Graphics Memory Interface"), url))
        except Exception as exc:
            skipped.append(f"{url}: {type(exc).__name__}: {exc}")
    if not output:
        raise RuntimeError("All Intel product pages failed: " + "; ".join(skipped))
    return output, skipped


def collect_gpus(brands=None, headless=True):
    """Called by your existing CoreBase.py. Returns only validated live records."""
    from playwright.sync_api import sync_playwright

    handlers = {"nvidia": collect_nvidia, "amd": collect_amd, "intel": collect_intel}
    selected = list(handlers) if brands is None else list(brands)
    if not selected or any(name not in handlers for name in selected):
        raise ValueError("brands must contain nvidia, amd, and/or intel")
    report = {"started_at_utc": datetime.now(timezone.utc).isoformat(), "brands": {}}
    output = {}
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=headless)
        try:
            context = browser.new_context(locale="en-US")
            for brand in selected:
                page = context.new_page()
                print(f"[CoreBase] Collecting {brand.upper()} ...", flush=True)
                try:
                    rows, warnings = handlers[brand](page)
                    staged = {}
                    for row in rows:
                        previous = staged.get(row["product_id"], output.get(row["product_id"]))
                        if previous is not None and previous != row:
                            raise ValueError(f"Conflicting product ID: {row['product_id']}")
                        staged[row["product_id"]] = row
                    output.update(staged)
                    report["brands"][brand] = {"status": "partial" if warnings else "ok",
                                               "records": len({r['product_id'] for r in rows}),
                                               "skipped": warnings}
                    print(f"[{brand}] {len(rows)} records, {len(warnings)} skipped", flush=True)
                    for warning in warnings:
                        LOG.warning("[%s] %s", brand, warning)
                except Exception as exc:
                    report["brands"][brand] = {"status": "failed", "error": str(exc)}
                    LOG.error("[%s] Collection failed: %s", brand, exc)
                finally:
                    page.close()
        finally:
            browser.close()
    report["finished_at_utc"] = datetime.now(timezone.utc).isoformat()
    report["total_records"] = len(output)
    report_path = Path(__file__).resolve().parent / "collector_report.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"[CoreBase] Report: {report_path}", flush=True)
    if not output:
        raise RuntimeError("No official data collected. Database was not updated. See collector_report.json.")
    return list(output.values())


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Preview official GPU data without writing SQLite")
    parser.add_argument("--brand", choices=["all", "nvidia", "amd", "intel"], default="all")
    parser.add_argument("--show-browser", action="store_true")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    result = collect_gpus(None if args.brand == "all" else [args.brand], headless=not args.show_browser)
    print(json.dumps(result, ensure_ascii=False, indent=2))
