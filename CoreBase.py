from collector import collect_and_save_gpus
from database import DB_PATH, get_gpus


def main():
    gpus = collect_and_save_gpus()
    print(f"本次处理了 {len(gpus)} 条显卡数据")

    # 默认显示桌面显卡。
    for gpu in get_gpus():
        print(
            f"{gpu['model']} | "
            f"显存：{gpu['vram_gb']} GB | "
            f"位宽：{gpu['bus_bits']} bit"
        )

    print(f"数据库已保存到：{DB_PATH}")


if __name__ == "__main__":
    main()
