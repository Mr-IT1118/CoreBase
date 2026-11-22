def collect_gpus():
    """目前返回模拟数据，之后替换为真实采集代码。"""
    return [
        {
            "product_id": "demo_001",
            "model": "示例显卡 A",
            "vram_gb": 16,
            "bus_bits": 256,
            "source_url": None,
        },
        {
            "product_id": "demo_002",
            "model": "示例显卡 B",
            "vram_gb": 8,
            "bus_bits": 128,
            "source_url": None,
        },
    ]