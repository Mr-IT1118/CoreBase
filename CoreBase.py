from collector import collect_gpus
from database import DB_PATH, init_database, save_gpus, get_gpus


def main():
    # 1. 准备数据库
    init_database()

    # 2. 获取显卡数据
    gpus = collect_gpus()

    # 3. 保存到数据库
    save_gpus(gpus)
    print(f"本次处理了 {len(gpus)} 条显卡数据")

    # 4. 查询并显示
    for gpu in get_gpus():
        print(
            f"{gpu['model']} | "
            f"显存：{gpu['vram_gb']} GB | "
            f"位宽：{gpu['bus_bits']} bit"
        )

    print(f"数据库已保存到：{DB_PATH}")


if __name__ == "__main__":
    main()