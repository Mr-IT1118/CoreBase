"""CoreBase official GPU collector. Requires: pip install playwright.

Keeps the existing collect_gpus() interface and five-column SQLite schema.
Only complete desktop GPU records are returned. No guessed specifications.
"""

import json
import logging
import re
import time
from datetime import datetime, timezone
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
            ))
        except ValueError as exc:
            skipped.append(f"{name}: {exc}")
    if not output:
        raise ValueError("No complete AMD desktop specifications found")
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
        browser = p.chromium.launch(headless=headless, args=["--disable-http2"])
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


def main(argv=None):
    import argparse

    parser = argparse.ArgumentParser(description="Collect official GPU data; preview unless --save is given")
    parser.add_argument("--brand", type=str.lower, choices=["all", "nvidia", "amd", "intel"], default="all")
    parser.add_argument("--show-browser", action="store_true")
    parser.add_argument("--save", action="store_true", help="Upsert validated records using database.py")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    try:
        result = collect_gpus(None if args.brand == "all" else [args.brand], headless=not args.show_browser)
    except (RuntimeError, ImportError) as exc:
        LOG.error("%s", exc)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    if args.save:
        from database import DB_PATH, init_database, save_gpus
        init_database()
        save_gpus(result)
        print(f"[CoreBase] Saved {len(result)} records to {DB_PATH}", flush=True)
    else:
        print("[CoreBase] Preview only. Add --save to write these records to hardware.db.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
