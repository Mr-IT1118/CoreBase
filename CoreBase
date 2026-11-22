import sqlite3
from pathlib import Path

# 数据库保存在这个 Python 文件旁边
db_path = Path(__file__).parent / "hardware.db"
db = sqlite3.connect(db_path)

# 创建显卡数据表（已存在就跳过）
db.execute("""
CREATE TABLE IF NOT EXISTS gpus (
    product_id TEXT PRIMARY KEY,
    model TEXT NOT NULL,
    vram_gb INTEGER,
    bus_bits INTEGER,
    source_url TEXT
)
""")

# 模拟 Python 收集到的资料
# 之后把这里替换成采集程序的输出
collected_data = [
    ("demo_001", "示例显卡 A", 16, 256, None),
    ("demo_002", "示例显卡 B", 8, 128, None)
]

# 批量存入数据库；相同编号再次采集时更新参数
db.executemany("""
INSERT INTO gpus
(product_id, model, vram_gb, bus_bits, source_url)
VALUES (?, ?, ?, ?, ?)
ON CONFLICT(product_id) DO UPDATE SET
    model = excluded.model,
    vram_gb = excluded.vram_gb,
    bus_bits = excluded.bus_bits,
    source_url = excluded.source_url
""", collected_data)

# 保存修改
db.commit()

# 读取数据，检查是否存入成功
for row in db.execute("SELECT * FROM gpus"):
    print(row)

db.close()
print(f"数据库已保存到：{db_path}")