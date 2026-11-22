import sqlite3
from pathlib import Path

DB_PATH = Path(__file__).resolve().parent / "hardware.db"


def init_database():
    """创建显卡表；已有表则保留。"""
    db = sqlite3.connect(DB_PATH)

    try:
        with db:
            db.execute("""
                CREATE TABLE IF NOT EXISTS gpus (
                    product_id TEXT PRIMARY KEY,
                    model TEXT NOT NULL,
                    vram_gb INTEGER,
                    bus_bits INTEGER,
                    source_url TEXT
                )
            """)
    finally:
        db.close()


def save_gpus(gpus):
    """批量保存显卡；相同 product_id 更新参数。"""
    db = sqlite3.connect(DB_PATH)

    try:
        with db:
            db.executemany("""
                INSERT INTO gpus (
                    product_id, model, vram_gb,
                    bus_bits, source_url
                )
                VALUES (
                    :product_id, :model, :vram_gb,
                    :bus_bits, :source_url
                )
                ON CONFLICT(product_id) DO UPDATE SET
                    model = excluded.model,
                    vram_gb = excluded.vram_gb,
                    bus_bits = excluded.bus_bits,
                    source_url = excluded.source_url
            """, gpus)
    finally:
        db.close()


def get_gpus():
    """查询所有显卡，返回字典列表。"""
    db = sqlite3.connect(DB_PATH)
    db.row_factory = sqlite3.Row

    try:
        rows = db.execute(
            "SELECT * FROM gpus ORDER BY model"
        ).fetchall()

        return [dict(row) for row in rows]
    finally:
        db.close()