from collector import collect_and_save_gpus
from database import DB_PATH, get_gpus


def format_gpu(gpu):
    parts = [gpu["model"]]
    if gpu.get("architecture"):
        parts.append(f"架构：{gpu['architecture']}")
    if gpu.get("compute_units") is not None:
        unit = gpu.get("compute_unit_type") or "核心数"
        parts.append(f"{unit}：{gpu['compute_units']}")
    base, boost = gpu.get("base_clock_mhz"), gpu.get("boost_clock_mhz")
    if base and boost:
        parts.append(f"主频/睿频：{base}/{boost} MHz")
    elif base:
        parts.append(f"主频：{base} MHz")
    elif boost:
        parts.append(f"睿频：{boost} MHz")
    vram_type = f" {gpu['vram_type']}" if gpu.get("vram_type") else ""
    parts.append(f"显存：{gpu['vram_gb']} GB{vram_type}")
    if gpu.get("bus_bits"):
        parts.append(f"位宽：{gpu['bus_bits']} bit")
    if gpu.get("bandwidth_gbps"):
        parts.append(f"带宽：{gpu['bandwidth_gbps']:g} GB/s")
    if gpu.get("tdp_w"):
        parts.append(f"功耗：{gpu['tdp_w']} W")
    if gpu.get("recommended_psu_w"):
        parts.append(f"推荐电源：{gpu['recommended_psu_w']} W")
    if gpu.get("pcie_interface"):
        parts.append(f"接口：{gpu['pcie_interface']}")
    return " | ".join(parts)


def main():
    gpus = collect_and_save_gpus()
    print(f"本次处理了 {len(gpus)} 条显卡数据")

    # 默认显示桌面显卡。
    for gpu in get_gpus():
        print(format_gpu(gpu))

    print(f"数据库已保存到：{DB_PATH}")


if __name__ == "__main__":
    main()
