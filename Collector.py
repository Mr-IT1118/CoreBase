"""CoreBase official GPU collector. Requires: pip install playwright.

Returns validated desktop GPU specifications consumed by the CoreBase database layer.
Only complete desktop GPU records are returned. No guessed specifications.
"""

import json
import logging
import re
import time
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation
from html import unescape
from pathlib import Path
from urllib.parse import urlparse

LOG = logging.getLogger(__name__)
NVIDIA_URL = "https://www.nvidia.com/en-us/geforce/graphics-cards/compare/"
AMD_URL = "https://www.amd.com/en/products/specifications/graphics.html"
INTEL_URL = "https://www.intel.com/content/www/us/en/ark/products/series/227960/intel-arc-dedicated-graphics-family.html"
AMD_DATA_SELECTOR = '.cmp-product-specs-table[data-json]'

# This is the public JSON attribute used to render AMD's specifications table.
# Read it once, instead of racing the table's client-side pagination.
AMD_DATA_JS = r"""() => {
    const nodes = document.querySelectorAll('.cmp-product-specs-table[data-json]');
    if (nodes.length !== 1) throw new Error('Expected one AMD specifications dataset');
    const node = nodes[0];
    return {
        payload: JSON.parse(node.getAttribute('data-json')),
        summary: node.querySelector('.dt-info')?.textContent || ''
    };
}"""

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


COMPUTE_UNIT_TYPES = {
    "nvidia": "CUDA Cores",
    "amd": "Stream Processors",
    "intel": "Xe-Cores",
}
_MISSING_TEXT = {"", "-", "--", "n/a", "na", "none", "tbd", "varies by manufacturer"}


def _clean_optional(value):
    if value is None:
        return None
    text = clean(str(value))
    return None if text.casefold() in _MISSING_TEXT else text


def parse_int_spec(value, *, min_val=1, max_val=1000000):
    text = _clean_optional(value)
    if not text:
        return None
    match = re.fullmatch(r"(\d[\d,]*)", text)
    if not match:
        return None
    num = int(match.group(1).replace(",", ""))
    return num if min_val <= num <= max_val else None


def parse_watts(value):
    text = _clean_optional(value)
    if not text:
        return None
    match = re.fullmatch(r"(\d[\d,]*)\s*W?", text, re.I)
    if not match:
        return None
    watts = int(match.group(1).replace(",", ""))
    return watts if 1 <= watts <= 2000 else None


def parse_clock_mhz(value, default_unit="MHz"):
    text = _clean_optional(value)
    if not text:
        return None
    match = re.fullmatch(r"(\d+(?:\.\d+)?)\s*(GHz|MHz)?", text, re.I)
    if not match:
        return None
    unit = (match.group(2) or default_unit).upper()
    try:
        mhz = Decimal(match.group(1)) * (1000 if unit == "GHZ" else 1)
    except InvalidOperation:
        return None
    val = int(round(mhz))
    return val if 1 <= val <= 20000 else None


def parse_bandwidth_gbps(value):
    text = _clean_optional(value)
    if not text:
        return None
    match = re.fullmatch(r"(\d+(?:\.\d+)?)\s*(?:GB/(?:s|sec)|GBps)", text, re.I)
    if not match:
        return None
    val = float(match.group(1))
    return val if 0 < val <= 100000 else None


def parse_year(value):
    text = _clean_optional(value)
    if not text:
        return None
    full = re.search(r"\b(?:19|20)\d{2}\b", text)
    if full:
        return int(full.group(0))
    short = re.fullmatch(r"(?:Q[1-4]\s*['\u2019]\s*|\d{1,2}/\d{1,2}/)(\d{2})", text, re.I)
    if short:
        yy = int(short.group(1))
        return (1900 if yy >= 70 else 2000) + yy
    return None


def _split_variants(value, count):
    text = _clean_optional(value)
    if not text:
        return [None] * count
    parts = [
        p.strip()
        for p in re.split(r"\s+/\s+|\s+or\s+|(?<=\d)/(?=\d)", text, flags=re.I)
        if p.strip()
    ]
    if len(parts) == count:
        return parts
    if len(parts) == 1:
        return parts * count
    return [None] * count


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


def records(vendor, model, memory, bus, source_url, identity=None, specs=None):
    variants = memory_variants(memory)
    widths = re.findall(r"(?<!\d)(\d+)\s*[-\u2013\u2014]?\s*bits?\b", bus, re.I)
    if len(widths) not in (1, len(variants)):
        raise ValueError(f"Missing or ambiguous memory interface: {bus!r}")
    if len(widths) == 1:
        widths *= len(variants)
    model = clean(model)
    if vendor == "nvidia" and not model.startswith("NVIDIA"):
        model = "NVIDIA " + model
    per_variant = specs if isinstance(specs, list) else [specs or {}] * len(variants)
    if len(per_variant) != len(variants):
        per_variant = [{}] * len(variants)
    output = []
    for (size, kind), width, extra in zip(variants, widths, per_variant):
        width = int(width)
        if not 0 < width <= 8192:
            raise ValueError(f"Invalid memory interface: {width}")
        # Capacity/type form part of the key: variants must not overwrite each other.
        product_id = f"{vendor}-{slug(identity or model)}-{size}gb-{kind.lower()}"
        compute_units = extra.get("compute_units")
        output.append({
            "product_id": product_id,
            "model": f"{model} [{size} GB {kind}]",
            "architecture": extra.get("architecture"),
            "compute_units": compute_units,
            "compute_unit_type": COMPUTE_UNIT_TYPES[vendor] if compute_units is not None else None,
            "base_clock_mhz": extra.get("base_clock_mhz"),
            "boost_clock_mhz": extra.get("boost_clock_mhz"),
            "vram_gb": size,
            "vram_type": kind,
            "bus_bits": width,
            "bandwidth_gbps": extra.get("bandwidth_gbps"),
            "tdp_w": extra.get("tdp_w"),
            "recommended_psu_w": extra.get("recommended_psu_w"),
            "pcie_interface": extra.get("pcie_interface"),
            "release_year": extra.get("release_year"),
            "source_url": source_url,
        })
    return output


def _nvidia_architecture(by_label, index):
    for key in ("NVIDIA Architecture", "NVIDIA Architecture Name"):
        row = by_label.get(key)
        if row:
            arch = _clean_optional(row[index])
            if arch:
                return arch
    shader_row = by_label.get("Shader Cores")
    if shader_row:
        raw = _clean_optional(shader_row[index])
        if raw:
            cleaned = re.sub(r"\s+\d+(?:\.\d+)?\s*TFLOPS$", "", raw, flags=re.I).strip()
            if cleaned and not cleaned.isdigit():
                return cleaned
    return None


def _nvidia_pcie(rows, by_label, index):
    pcie_row = by_label.get("PCI Express")
    if pcie_row:
        val = _clean_optional(pcie_row[index])
        if val and val.casefold() != "no":
            return f"PCIe {val}" if val.lower().startswith("gen ") else val
    for row in rows:
        if not row:
            continue
        match = re.fullmatch(r"PCI Express (Gen \d+)", clean(row[0]), re.I)
        if match and index < len(row) and clean(row[index]).casefold() == "yes":
            return f"PCIe {match.group(1)}"
    return None


def _nvidia_clock_row(by_label, base_name):
    for unit in ("GHz", "MHz"):
        row = by_label.get(f"{base_name} ({unit})")
        if row:
            return row, unit
    return None, None


def _nvidia_specs(rows, by_label, index, variant_count):
    cuda_row = by_label.get("NVIDIA CUDA Cores")
    base_row, base_unit = _nvidia_clock_row(by_label, "Base Clock")
    boost_row, boost_unit = _nvidia_clock_row(by_label, "Boost Clock")
    bw_row = by_label.get("Memory Bandwidth")
    tdp_row = by_label.get("Total Graphics Power (W)") or by_label.get("Graphics Card Power (W)")
    psu_row = next(
        (r for k, r in by_label.items()
         if k.startswith(("Required System Power (W)", "Minimum System Power (W)"))),
        None,
    )
    arch = _nvidia_architecture(by_label, index)
    pcie = _nvidia_pcie(rows, by_label, index)

    cudas = _split_variants(cuda_row[index] if cuda_row else None, variant_count)
    bases = _split_variants(base_row[index] if base_row else None, variant_count)
    boosts = _split_variants(boost_row[index] if boost_row else None, variant_count)
    bws = _split_variants(bw_row[index] if bw_row else None, variant_count)
    tdps = _split_variants(tdp_row[index] if tdp_row else None, variant_count)
    psus = _split_variants(psu_row[index] if psu_row else None, variant_count)

    return [{
        "architecture": arch,
        "compute_units": parse_int_spec(cudas[i], max_val=200000),
        "base_clock_mhz": parse_clock_mhz(bases[i], base_unit or "MHz"),
        "boost_clock_mhz": parse_clock_mhz(boosts[i], boost_unit or "MHz"),
        "bandwidth_gbps": parse_bandwidth_gbps(bws[i]),
        "tdp_w": parse_watts(tdps[i]),
        "recommended_psu_w": parse_watts(psus[i]),
        "pcie_interface": pcie,
    } for i in range(variant_count)]


def parse_nvidia(tables):
    output, skipped = [], []
    for table in tables:
        rows = [[cell["text"] for cell in row] for row in table]
        memory = next((r for r in rows if r and clean(r[0]) == "Standard Memory Config"), None)
        if memory is None:
            continue
        header = next((r for r in rows if sum("GeForce" in c for c in r) >= 1), None)
        bus = next((r for r in rows if r and clean(r[0]) == "Memory Interface Width"), None)
        if not header or not bus or not (len(header) == len(memory) == len(bus)):
            raise ValueError("NVIDIA table columns changed; refused to guess alignment")
        by_label = {}
        for row in rows:
            if row and len(row) == len(header):
                label = clean(row[0])
                if label and label not in by_label:
                    by_label[label] = row
        for index in range(1, len(header)):
            try:
                variant_count = len(memory_variants(memory[index]))
                specs = _nvidia_specs(rows, by_label, index, variant_count)
                output.extend(records("nvidia", header[index], memory[index], bus[index], NVIDIA_URL, specs=specs))
            except ValueError as exc:
                skipped.append(f"{header[index]}: {exc}")
    if not output:
        raise ValueError("No complete NVIDIA specification rows found")
    return output, skipped


def amd_field_values(field):
    """Use AMD's display values and explicit units, never raw enum codes."""
    if not isinstance(field, dict):
        raise ValueError("AMD field format changed")
    value = field.get("formatValue")
    if value is None:
        return []
    values = value if isinstance(value, list) else [value]
    suffix = unescape(field.get("suffix") or "")
    result = []
    for part in values:
        if isinstance(part, bool) or not isinstance(part, (str, int, float)):
            raise ValueError("Unsupported AMD field value")
        text = clean(unescape(str(part)))
        if not text:
            continue
        if suffix and not text.endswith(suffix.strip()):
            text += suffix
        result.append(clean(text))
    return result


def amd_expected_total(summary):
    """Cross-check against the page counter when the UI has rendered it."""
    if not summary.strip():
        return None  # The JSON can arrive before the table is rendered.
    match = re.fullmatch(
        r"Showing\s+[\d,]+\s+to\s+[\d,]+\s+of\s+([\d,]+)\s+entries",
        clean(summary), re.I,
    )
    if not match:
        raise ValueError("AMD entry counter changed or a table filter is active")
    return int(match.group(1).replace(",", ""))


def _amd_single(fields, key):
    if key not in fields:
        return None
    vals = amd_field_values(fields[key])
    return vals[0] if len(vals) == 1 else None


def _amd_architecture(fields):
    explicit = _clean_optional(_amd_single(fields, "architecture"))
    if explicit:
        return re.sub(r"\s+Architecture$", "", explicit, flags=re.I)
    if "supportedTechnologies" not in fields:
        return None
    techs = amd_field_values(fields["supportedTechnologies"])
    arch_hits = [re.sub(r"\s+Architecture$", "", t, flags=re.I) for t in techs
                 if re.search(r"\bArchitecture$", t, re.I)]
    return arch_hits[0] if len(arch_hits) == 1 else None


def _amd_specs(fields):
    base_raw = _amd_single(fields, "baseFrequency") or _amd_single(fields, "gameFrequency")
    pcie_raw = _clean_optional(_amd_single(fields, "busType"))
    if pcie_raw and "pci" not in pcie_raw.casefold():
        pcie_raw = None
    return {
        "architecture": _amd_architecture(fields),
        "compute_units": parse_int_spec(_amd_single(fields, "streamProcessors"), max_val=200000),
        "base_clock_mhz": parse_clock_mhz(base_raw),
        "boost_clock_mhz": parse_clock_mhz(_amd_single(fields, "boostFrequency")),
        "bandwidth_gbps": parse_bandwidth_gbps(_amd_single(fields, "memoryBandwidth")),
        "tdp_w": parse_watts(_amd_single(fields, "typicalBoardPowerDesktop")),
        "recommended_psu_w": parse_watts(_amd_single(fields, "minimumPsuRecommendation")),
        "pcie_interface": pcie_raw,
        "release_year": parse_year(_amd_single(fields, "launchDate")),
    }


def parse_amd_payload(payload, expected_total=None):
    """Parse every product in AMD's public table dataset, not one display page."""
    items = payload.get("items") if isinstance(payload, dict) else None
    if not isinstance(items, list) or not items:
        raise ValueError("AMD specifications dataset is empty or its format changed")
    if expected_total is not None and len(items) != expected_total:
        raise ValueError(f"AMD dataset has {len(items)} items; page reports {expected_total}")
    required = {"name", "boardType", "maxMemorySize", "memoryType", "memoryInterface"}
    output, skipped, seen_names = [], [], set()
    for index, item in enumerate(items):
        fields = item.get("elements") if isinstance(item, dict) else None
        if not isinstance(fields, dict) or not required <= fields.keys():
            raise ValueError(f"AMD item {index + 1}: required specification fields changed")
        names = amd_field_values(fields["name"])
        if len(names) != 1 or not names[0]:
            raise ValueError(f"AMD item {index + 1}: missing or ambiguous product name")
        name = names[0]
        if name in seen_names:
            raise ValueError(f"AMD dataset repeated a product: {name}")
        seen_names.add(name)
        boards = set(amd_field_values(fields["boardType"]))
        if boards == {"Notebook"}:
            continue
        # Combined Desktop/Notebook entries do not identify a desktop variant.
        if not boards or not boards <= {"Desktop", "Component"}:
            skipped.append(f"{name}: desktop variant is not unambiguous ({sorted(boards)})")
            continue
        try:
            sizes = amd_field_values(fields["maxMemorySize"])
            kinds = amd_field_values(fields["memoryType"])
            # 'Max Memory Size' cannot establish capacities for multiple types.
            if len(sizes) != 1 or len(kinds) != 1:
                raise ValueError("Missing or ambiguous memory capacity/type")
            output.extend(records(
                "amd", name, sizes[0] + " " + kinds[0],
                " / ".join(amd_field_values(fields["memoryInterface"])), AMD_URL,
                specs=_amd_specs(fields),
            ))
        except ValueError as exc:
            skipped.append(f"{name}: {exc}")
    if not output:
        raise ValueError("No complete AMD desktop specifications found")
    return output, skipped


def parse_intel(tables, source_url):
    data = {clean(row[0]["text"]).replace("\u2021", "").replace("\u2020", "").strip().casefold():
            clean(row[1]["text"])
            for table in tables for row in table if len(row) == 2}

    def get(*labels):
        return next((data[k] for k in labels if data.get(k)), None)

    if get("vertical segment") != "Desktop":
        raise ValueError("Intel product is not confirmed as a desktop GPU")
    model = get("model number") or ""
    if not re.fullmatch(r"[A-Z]\d{3}", model):
        raise ValueError("Intel desktop model number was not found")
    sku = re.search(r"/sku/(\d+)/", source_url)
    if not sku:
        raise ValueError("Intel SKU was not found in the source URL")
    # B-series combines these fields; A-series lists capacity and type separately.
    memory = get("memory") or ((get("memory size") or "") + " " + (get("memory type") or ""))
    specs = {
        "architecture": _clean_optional(get("microarchitecture", "graphics microarchitecture")),
        "compute_units": parse_int_spec(get("xe-cores", "xe cores"), max_val=10000),
        "base_clock_mhz": parse_clock_mhz(get("graphics clock")),
        "boost_clock_mhz": parse_clock_mhz(
            get("graphics max dynamic frequency", "max dynamic frequency", "boost clock")
        ),
        "bandwidth_gbps": parse_bandwidth_gbps(get("graphics memory bandwidth", "memory bandwidth")),
        "tdp_w": parse_watts(get("total board power (tbp)", "total board power", "tdp")),
        "pcie_interface": _clean_optional(get("pci express configurations", "pci express revision")),
        "release_year": parse_year(get("launch date")),
    }
    return records("intel", f"Intel Arc {model}", memory,
                   get("graphics memory interface") or "", source_url,
                   identity="sku-" + sku.group(1), specs=specs)


def navigate(page, url, domain):
    from playwright.sync_api import Error as BrowserError, TimeoutError as BrowserTimeout

    # The source can be ready while third-party scripts delay DOMContentLoaded.
    # Each collector waits for its actual data after the main response commits.
    for attempt in range(2):
        time.sleep(2 if attempt == 0 else 3)
        LOG.info("Opening %s (attempt %s/2)", url, attempt + 1)
        try:
            response = page.goto(url, wait_until="commit", timeout=60000)
            break
        except BrowserError as exc:
            transient = isinstance(exc, BrowserTimeout) or any(code in str(exc) for code in (
                "ERR_HTTP2_PROTOCOL_ERROR", "ERR_CONNECTION_RESET", "ERR_TIMED_OUT",
            ))
            if not transient or attempt == 1:
                raise RuntimeError(
                    f"Could not open {url}: {exc}. Try --show-browser and check that "
                    "the official page opens in your browser."
                ) from exc
            LOG.warning("Temporary navigation failure; retrying once: %s", exc)
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
    page.wait_for_function(
        """selector => {
            const node = document.querySelector(selector);
            if (!node) return false;
            try {
                const data = JSON.parse(node.getAttribute('data-json'));
                return Array.isArray(data.items) && data.items.length > 0;
            } catch (_) { return false; }
        }""",
        arg=AMD_DATA_SELECTOR, timeout=30000,
    )
    snapshot = page.evaluate(AMD_DATA_JS)
    expected_total = amd_expected_total(snapshot["summary"])
    output, skipped = parse_amd_payload(snapshot["payload"], expected_total)
    LOG.info("AMD: inspected all %s source items without pagination",
             len(snapshot["payload"]["items"]))
    return output, skipped


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


def collect_gpus(brands=None, headless=True, *, report=None):
    """Called by your existing CoreBase.py. Returns only validated live records."""
    handlers = {"nvidia": collect_nvidia, "amd": collect_amd, "intel": collect_intel}
    selected = list(handlers) if brands is None else list(brands)
    if not selected or any(name not in handlers for name in selected):
        raise ValueError("brands must contain nvidia, amd, and/or intel")
    if report is None:
        report = {}
    report.update(started_at_utc=datetime.now(timezone.utc).isoformat(), brands={})
    from playwright.sync_api import sync_playwright
    output = {}
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=headless, args=["--disable-http2"])
        try:
            context = browser.new_context(locale="en-US")
            for brand in selected:
                started = datetime.now(timezone.utc).isoformat()
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
                    report["brands"][brand]["started_at"] = started
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


def collect_and_save_gpus(brands=None, headless=True):
    """Persist the specs and audit result from the same in-memory crawl."""
    import database

    selected = ["nvidia", "amd", "intel"] if brands is None else list(brands)
    report = {}
    try:
        rows = collect_gpus(selected, headless=headless, report=report)
    except Exception as exc:
        if report:
            for brand in selected:
                details = report["brands"].get(brand, {})
                database.record_sync_failure(
                    "GPU", brand, details.get("error") or str(exc),
                    details.get("started_at", report["started_at_utc"]), len(details.get("skipped", ())),
                )
        raise
    database.init_database()
    details_by_brand = report.get("brands", {})
    if not details_by_brand:
        database.save_gpus(rows)
    for brand, details in details_by_brand.items():
        started = details["started_at"]
        if details["status"] == "failed":
            database.record_sync_failure("GPU", brand, details["error"], started)
            continue
        selected_rows = [row for row in rows if row["product_id"].split("-", 1)[0] == brand]
        warnings = details.get("skipped", [])
        database.save_gpus(selected_rows, brand=brand, started_at=started,
                           skipped=len(warnings), error_message="\n".join(warnings) or None)
    return rows


def main(argv=None):
    import argparse

    parser = argparse.ArgumentParser(description="Collect official GPU data; preview unless --save is given")
    parser.add_argument("--brand", type=str.lower, choices=["all", "nvidia", "amd", "intel"], default="all")
    parser.add_argument("--show-browser", action="store_true")
    parser.add_argument("--save", action="store_true", help="Upsert validated records using database.py")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    try:
        run = collect_and_save_gpus if args.save else collect_gpus
        result = run(None if args.brand == "all" else [args.brand], headless=not args.show_browser)
    except (RuntimeError, ImportError) as exc:
        LOG.error("%s", exc)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    if args.save:
        from database import DB_PATH
        print(f"[CoreBase] Saved {len(result)} records to {DB_PATH}", flush=True)
    else:
        print("[CoreBase] Preview only. Add --save to write these records to hardware.db.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
