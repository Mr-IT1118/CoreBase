"""CoreBase SQLite storage. GPU clocks use MHz, CPU clocks GHz, bandwidth GB/s."""
import re
import sqlite3
from contextlib import closing
from datetime import datetime, timezone
from decimal import Decimal
from pathlib import Path

DB_PATH = Path(__file__).resolve().with_name("hardware.db")
SCHEMA_VERSION = 1
BRANDS = {"nvidia": "NVIDIA", "amd": "AMD", "intel": "Intel"}
GPU_FIELDS = """product_id brand series model form_factor architecture compute_units
    compute_unit_type base_clock_mhz boost_clock_mhz vram_gb vram_type bus_bits
    bandwidth_gbps tdp_w recommended_psu_w pcie_interface release_year source_url""".split()
CPU_FIELDS = """product_id brand series model form_factor architecture cores p_cores
    e_cores threads base_clock_ghz boost_clock_ghz l2_cache_mb l3_cache_mb socket
    memory_support pcie_version tdp_w max_turbo_power_w igpu_model release_year source_url""".split()
STAMP = "strftime('%Y-%m-%dT%H:%M:%fZ','now')"
MEMORY = r"(?:GDDR[0-9]+X?|DDR[0-9]+|HBM(?:[0-9]+E?)?)"
MEM_SUFFIX = re.compile(r"\s*\[\s*(\d+)\s*GB\s+(" + MEMORY + r")\s*\]\s*$", re.I)
KEY_SUFFIX = re.compile(r"-(\d+)gb-(" + MEMORY + r")$", re.I)
CLOCK_MHZ = {"MHz": 1, "GHz": 1000}
CLOCK_GHZ = {"GHz": 1, "MHz": Decimal("0.001")}
CACHE_MB = {"MB": 1, "KB": Decimal(1) / 1024}
POWER_W = {"W": 1, "kW": 1000}
UNIT_SCALES = {
    "base_clock_mhz": CLOCK_MHZ, "boost_clock_mhz": CLOCK_MHZ,
    "base_clock_ghz": CLOCK_GHZ, "boost_clock_ghz": CLOCK_GHZ,
    "l2_cache_mb": CACHE_MB, "l3_cache_mb": CACHE_MB,
    "bus_bits": {"bit": 1, "bits": 1},
    "bandwidth_gbps": {"GB/s": 1, "GBps": 1, "Gb/s": Decimal("0.125"),
                       "Gbps": Decimal("0.125"), "MB/s": Decimal("0.001")},
    "tdp_w": POWER_W, "recommended_psu_w": POWER_W, "max_turbo_power_w": POWER_W,
}
COMPUTE_TYPES = {"NVIDIA": "CUDA Cores", "AMD": "Stream Processors", "Intel": "Xe-Cores"}


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def connect():
    db = sqlite3.connect(DB_PATH, timeout=15)
    try:
        db.row_factory = sqlite3.Row
        if db.execute("PRAGMA journal_mode=WAL").fetchone()[0] != "wal":
            raise RuntimeError("CoreBase needs a local database that supports WAL.")
        db.execute("PRAGMA foreign_keys=ON")
        db.execute("PRAGMA cache_size=-16384")
        db.execute("PRAGMA temp_store=MEMORY")
        return db
    except BaseException:
        db.close()
        raise


def _brand(value):
    try:
        return BRANDS[str(value).casefold()]
    except KeyError:
        raise ValueError(f"Unsupported hardware brand: {value!r}") from None


def _slug(model):
    value = re.sub("[^a-z0-9]+", "-", model.casefold()).strip("-")
    if not value:
        raise ValueError("Missing model name")
    return value


def _series(model):
    patterns = (
        (r"GeForce (RTX|GTX) (\d{2})\d{2}", r"GeForce \1 \2 Series"),
        (r"Radeon RX (\d)\d{3}", r"Radeon RX \g<1>000 Series"),
        (r"Radeon RX (\d)\d{2}\b", r"Radeon RX \g<1>00 Series"),
        (r"(Radeon (?:R[579]|HD|RX Vega))\b", r"\1"),
        (r"Arc ([AB])\d{3}", r"Arc \1-Series"),
    )
    for pattern, template in patterns:
        match = re.search(pattern, model, re.I)
        if match:
            return match.expand(template)
    return None


def _gpu_form(model):
    if re.search(r"\b(laptop|notebook|mobile|M\d{3}X?|\d{3,4}M(?:X)?)\b", model, re.I):
        return "Mobile"
    if re.search(r"\b(Quadro|Radeon Pro|Arc Pro|RTX A\d{3,4}|RTX PRO)\b", model, re.I):
        return "Workstation"
    return None


def _normalize(category, record, existing=None):
    """Normalize official five-column rows and typed rows from other sources."""
    row = {k: (v.strip() if isinstance(v, str) else v) for k, v in dict(record).items()}
    row = {k: v for k, v in row.items() if v is not None and v != ""}
    for field, scales in UNIT_SCALES.items():
        value = row.get(field)
        if isinstance(value, str):
            match = re.fullmatch(r"(\d+(?:\.\d+)?)\s*-?\s*([A-Za-z/]+)", value)
            if match:
                if match[2] not in scales:
                    raise ValueError(f"Unsupported unit for {field}: {match[2]}")
                row[field] = str(Decimal(match[1]) * scales[match[2]])
    old_key = str(row.get("product_id", ""))
    known = dict(existing or {})
    model = " ".join(str(row.get("model", known.get("model", ""))).split())
    if not model:
        raise ValueError(f"Missing model for {old_key!r}")
    guess = (re.match(r"^(NVIDIA|AMD|Intel)\b", old_key, re.I)
             or re.match(r"^(NVIDIA|AMD|Intel)\b", model, re.I))
    brand = _brand(row.get("brand") or known.get("brand") or (guess and guess[1]))
    model = re.sub(r"^(?:" + brand + r"\s+)+", "", model, flags=re.I)
    if category == "GPU":
        bracket = MEM_SUFFIX.search(model)
        suffix = KEY_SUFFIX.search(old_key)
        sizes = [int(x[1]) for x in (bracket, suffix) if x]
        kinds = [x[2].upper() for x in (bracket, suffix) if x]
        size = row.get("vram_gb", known.get("vram_gb"))
        if size is not None:
            if not re.fullmatch(r"\d+", str(size)):
                raise ValueError(f"Expected whole GB for {model}: {size!r}")
            sizes.append(int(size))
        kind = row.get("vram_type", known.get("vram_type"))
        if kind:
            kinds.append(str(kind).strip().upper())
        if (len(set(sizes)) != 1 or sizes[0] <= 0 or len(set(kinds)) != 1
                or not re.fullmatch(MEMORY, kinds[0])):
            raise ValueError(f"Missing or conflicting VRAM identity for {model}: {sizes}, {kinds}")
        size, kind = sizes[0], kinds[0]
        model = MEM_SUFFIX.sub("", model).strip()
        model = re.sub(r"\s*\(\s*" + str(size) + r"\s*GB\s*\)\s*$", "", model, flags=re.I)
        model = re.sub(r"\s+" + str(size) + r"\s*GB\s*$", "", model, flags=re.I)
        row.update(vram_gb=size, vram_type=kind)
        if row.get("compute_units") is not None:
            row.setdefault("compute_unit_type", COMPUTE_TYPES[brand])
        row.setdefault("series", _series(model))
        row.setdefault("form_factor", known.get("form_factor") or _gpu_form(model))
        key = f"{brand.lower()}-{_slug(model)}-{size}gb-{kind.lower()}"
    else:
        key = f"{brand.lower()}-{_slug(model)}"
    row.update(brand=brand, model=f"{brand} {model}", product_id=key)
    return row


def _create_main(db, table):
    gpu = table == "gpus"
    brands = "'AMD','Intel','NVIDIA'" if gpu else "'AMD','Intel'"
    specs = """
        compute_units INTEGER CHECK(compute_units > 0),
        compute_unit_type TEXT,
        base_clock_mhz INTEGER CHECK(base_clock_mhz > 0),
        boost_clock_mhz INTEGER CHECK(boost_clock_mhz > 0),
        vram_gb INTEGER NOT NULL CHECK(vram_gb > 0),
        vram_type TEXT NOT NULL,
        bus_bits INTEGER CHECK(bus_bits > 0),
        bandwidth_gbps REAL CHECK(bandwidth_gbps > 0),
        recommended_psu_w INTEGER CHECK(recommended_psu_w > 0),
        pcie_interface TEXT
    """ if gpu else """
        cores INTEGER CHECK(cores > 0),
        p_cores INTEGER CHECK(p_cores >= 0),
        e_cores INTEGER CHECK(e_cores >= 0),
        threads INTEGER CHECK(threads >= cores AND threads > 0),
        base_clock_ghz REAL CHECK(base_clock_ghz > 0),
        boost_clock_ghz REAL CHECK(boost_clock_ghz > 0),
        l2_cache_mb REAL CHECK(l2_cache_mb >= 0),
        l3_cache_mb INTEGER CHECK(l3_cache_mb >= 0),
        socket TEXT, memory_support TEXT, pcie_version TEXT,
        max_turbo_power_w INTEGER CHECK(max_turbo_power_w > 0),
        igpu_model TEXT
    """
    checks = """
        CHECK (compute_unit_type IS NULL OR compute_unit_type =
            CASE brand WHEN 'AMD' THEN 'Stream Processors'
                       WHEN 'Intel' THEN 'Xe-Cores' ELSE 'CUDA Cores' END),
        CHECK (compute_units IS NULL OR compute_unit_type IS NOT NULL)
    """ if gpu else """
        CHECK (cores IS NULL OR cores >= coalesce(p_cores,0) + coalesce(e_cores,0)),
        CHECK (brand != 'Intel' OR upper(substr(model,-1)) != 'F' OR igpu_model IS NULL)
    """
    db.execute(f"""CREATE TABLE {table} (
        product_id TEXT PRIMARY KEY NOT NULL, brand TEXT NOT NULL CHECK(brand IN ({brands})),
        series TEXT, model TEXT NOT NULL,
        form_factor TEXT NOT NULL DEFAULT 'Desktop'
            CHECK(form_factor IN ('Desktop','Mobile','Workstation')),
        architecture TEXT, {specs}, tdp_w INTEGER CHECK(tdp_w > 0),
        release_year INTEGER, source_url TEXT,
        created_at TEXT NOT NULL DEFAULT ({STAMP}), updated_at TEXT NOT NULL DEFAULT ({STAMP}),
        {checks}
    ) STRICT""")


def _create_search(db):
    db.execute("""CREATE VIRTUAL TABLE hardware_fts USING fts5(
        product_id UNINDEXED, category, brand, model, search_tags, tokenize='unicode61')""")
    for table, category in (("gpus", "GPU"), ("cpus", "CPU")):
        # coalesce preserves searchable tags even when some specifications are unknown.
        tags = ["new.series", "new.architecture", "new.form_factor"]
        if category == "GPU":
            tags += ["new.vram_gb || 'g'", "new.vram_gb || 'gb'", "new.vram_type",
                     "new.bus_bits || 'bit'", "replace(lower(new.model),' ti','ti')", "new.pcie_interface"]
        else:
            tags += ["new.socket", "new.l3_cache_mb || 'mb'", "new.memory_support",
                     "new.cores || 'c'", "new.threads || 't'", "new.igpu_model"]
        tag_expr = " || ' ' || ".join(f"coalesce({part},'')" for part in tags)
        insert = f"""INSERT INTO hardware_fts(product_id,category,brand,model,search_tags)
            VALUES(new.product_id,'{category}',new.brand,new.model,trim({tag_expr}));"""
        delete = f"DELETE FROM hardware_fts WHERE product_id=old.product_id AND category='{category}';"
        for event, statements in (("INSERT", insert), ("UPDATE", delete + insert), ("DELETE", delete)):
            db.execute(f"CREATE TRIGGER {table}_fts_{event} AFTER {event} ON {table} BEGIN {statements} END")


def _backup_locked():
    # init_database holds a writer reservation. A second connection can read all committed WAL pages.
    folder = DB_PATH.parent / "backups"
    folder.mkdir(exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    path = folder / f"{DB_PATH.stem}-v0-{stamp}.db"
    path.touch(exist_ok=False)
    try:
        uri = DB_PATH.resolve().as_uri() + "?mode=ro"
        with closing(sqlite3.connect(uri, uri=True)) as source, closing(sqlite3.connect(path)) as target:
            source.backup(target)
            if target.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
                raise RuntimeError("Backup integrity check failed")
    except BaseException:
        path.unlink(missing_ok=True)
        raise


def init_database():
    """Upgrade once, under a writer lock, with an independent pre-upgrade backup."""
    with closing(connect()) as db, db:
        version = db.execute("PRAGMA user_version").fetchone()[0]
        if version == SCHEMA_VERSION:
            return
        db.execute("BEGIN IMMEDIATE")
        version = db.execute("PRAGMA user_version").fetchone()[0]
        if version == SCHEMA_VERSION:
            return
        if version != 0:
            raise RuntimeError(f"Unsupported database version {version}")
        objects = db.execute("SELECT name FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'").fetchall()
        if objects:
            _backup_locked()
        if {r[0] for r in objects} - {"gpus"}:
            raise ValueError("Unrecognized v0 objects. Upgrade stopped to preserve the existing schema.")
        if objects:
            columns = {r["name"] for r in db.execute("PRAGMA table_info(gpus)")}
            if not {"product_id", "model"} <= columns or columns - set(GPU_FIELDS + ["created_at", "updated_at"]):
                raise ValueError("Unrecognized legacy GPU columns")
            db.execute("ALTER TABLE gpus RENAME TO gpus_v0")
        _create_main(db, "gpus")
        _create_main(db, "cpus")
        db.execute("""CREATE TABLE sync_logs (
            id INTEGER PRIMARY KEY, category TEXT NOT NULL CHECK(category IN ('GPU','CPU')),
            brand TEXT NOT NULL CHECK(brand IN ('AMD','Intel','NVIDIA') AND (category='GPU' OR brand!='NVIDIA')),
            status TEXT NOT NULL CHECK(status IN ('success','partial','failed')),
            records_upserted INTEGER NOT NULL CHECK(records_upserted >= 0),
            records_skipped INTEGER NOT NULL CHECK(records_skipped >= 0),
            error_message TEXT, started_at TEXT NOT NULL, finished_at TEXT NOT NULL
        ) STRICT""")
        db.execute("CREATE INDEX gpus_brand_form_vram ON gpus(brand,form_factor,vram_gb)")
        db.execute("CREATE INDEX gpus_memory_bus ON gpus(vram_type,bus_bits)")
        db.execute("CREATE INDEX cpus_brand_socket_cores ON cpus(brand,socket,cores)")
        db.execute("CREATE INDEX cpus_l3_cache ON cpus(l3_cache_mb)")
        _create_search(db)
        if objects:
            for old in db.execute("SELECT * FROM gpus_v0 WHERE product_id NOT GLOB 'demo_*' OR product_id IS NULL"):
                row = _normalize("GPU", old)
                fields = [k for k in GPU_FIELDS + ["created_at", "updated_at"] if row.get(k) is not None]
                # A canonical key collision aborts the migration instead of discarding a variant.
                db.execute(f"INSERT INTO gpus ({','.join(fields)}) VALUES ({','.join(':'+k for k in fields)})", row)
            db.execute("DROP TABLE gpus_v0")
        db.execute(f"PRAGMA user_version={SCHEMA_VERSION}")


def _audit(db, category, brand, status, count, skipped, error, started):
    db.execute("""INSERT INTO sync_logs
        (category,brand,status,records_upserted,records_skipped,error_message,started_at,finished_at)
        VALUES (?,?,?,?,?,?,?,?)""", (category, brand, status, count, skipped, error, started, utc_now()))


def record_sync_failure(category, brand, error, started_at, skipped=0):
    init_database()
    with closing(connect()) as db, db:
        _audit(db, category, _brand(brand), "failed", 0, skipped, str(error), started_at)


def _save(category, records, brand=None, started_at=None, skipped=0, error_message=None):
    init_database()
    started = started_at or utc_now()
    table, fields = ("gpus", GPU_FIELDS) if category == "GPU" else ("cpus", CPU_FIELDS)
    # form_factor defaults only on INSERT. Unspecified fields do not erase newer data.
    values = [("coalesce(:form_factor,'Desktop')" if k == "form_factor" else ":" + k) for k in fields]
    updates = []
    for field in fields[1:]:
        if field in ("series", "form_factor"):
            updates.append(f"{field}=CASE WHEN :given_{field} THEN :{field} "
                           f"ELSE coalesce({table}.{field},:{field}) END")
        else:
            updates.append(f"{field}=coalesce(:{field},{table}.{field})")
    sql = (f"INSERT INTO {table} ({','.join(fields)}) VALUES ({','.join(values)}) "
           f"ON CONFLICT(product_id) DO UPDATE SET {','.join(updates)}, updated_at={STAMP}")
    brand = _brand(brand) if brand else None
    counts = {}
    try:
        with closing(connect()) as db, db:
            db.execute("BEGIN IMMEDIATE")
            for item in records:
                key = item.get("product_id")
                existing = db.execute(f"SELECT * FROM {table} WHERE product_id=?", (key,)).fetchone() if key else None
                row = _normalize(category, item, existing)
                if existing and existing["product_id"] != row["product_id"]:
                    raise ValueError(f"Identity fields disagree with existing key {key}")
                if brand and row["brand"] != brand:
                    raise ValueError(f"Row brand {row['brand']} differs from batch brand {brand}")
                params = {k: row.get(k) for k in fields}
                for field in ("series", "form_factor"):
                    params["given_" + field] = bool(str(item.get(field) or "").strip())
                db.execute(sql, params)
                counts.setdefault(row["brand"], set()).add(row["product_id"])
            if brand:
                counts.setdefault(brand, set())
            for vendor, keys in counts.items():
                _audit(db, category, vendor, "partial" if skipped else "success",
                       len(keys), skipped, error_message, started)
    except Exception as exc:
        if brand:
            record_sync_failure(category, brand, exc, started, skipped)
        raise


def save_gpus(gpus, **batch):
    """Accept existing collector dictionaries or normalized specification rows."""
    _save("GPU", gpus, **batch)


def save_cpus(cpus, **batch):
    _save("CPU", cpus, **batch)


def get_gpus(form_factor="Desktop"):
    """Pass None to include mobile and workstation GPUs."""
    init_database()
    with closing(connect()) as db:
        sql = "SELECT * FROM gpus"
        params = ()
        if form_factor is not None:
            sql += " WHERE form_factor=?"
            params = (form_factor,)
        return [dict(r) for r in db.execute(sql + " ORDER BY model, vram_gb, vram_type", params)]


def search_hardware(query):
    """Match all user-entered words, safely treating FTS operators as ordinary text."""
    words = re.findall(r"[^\W_]+", query, re.UNICODE)
    if not words:
        return []
    init_database()
    with closing(connect()) as db:
        expr = " AND ".join('"' + word + '"' for word in words)
        return [dict(r) for r in db.execute(
            "SELECT * FROM hardware_fts WHERE hardware_fts MATCH ? ORDER BY rank,product_id", (expr,))]


if __name__ == "__main__":
    init_database()
    print(f"CoreBase database v{SCHEMA_VERSION}: {DB_PATH}")

