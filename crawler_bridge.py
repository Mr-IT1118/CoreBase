"""Run the existing collectors for the desktop UI and publish their result as UTF-8 JSON."""
import argparse
import json
import os
from pathlib import Path
import sqlite3
import sys
from contextlib import closing

import collector
import cpu_collector
import database


def checkpoint():
    with closing(sqlite3.connect(database.DB_PATH, timeout=15)) as db:
        busy, _, _ = db.execute("PRAGMA wal_checkpoint(TRUNCATE)").fetchone()
        if busy:
            raise RuntimeError("数据库正被占用，已提交的数据仍保存在 WAL 中")


def last_gpu_result(brand, started):
    # The collector already records per-brand warnings and errors in this table.
    if not database.DB_PATH.exists():
        return None
    with closing(sqlite3.connect(database.DB_PATH.as_uri() + "?mode=ro", uri=True)) as db:
        return db.execute(
            "SELECT status, records_skipped, error_message FROM sync_logs "
            "WHERE category='GPU' AND brand=? AND finished_at>=? ORDER BY id DESC LIMIT 1",
            (database.BRANDS[brand], started),
        ).fetchone()


def error_message(exc):
    detail = str(exc)
    if "playwright" in detail.lower() and (
        isinstance(exc, ImportError) or isinstance(exc.__cause__, ImportError)
    ):
        return "缺少 Playwright。请在所选 Python 中运行 python -m pip install playwright，再运行 python -m playwright install chromium。"
    if "executable doesn't exist" in detail.lower():
        return "未找到 Chromium。请运行 python -m playwright install chromium。"
    return "采集失败，请检查网络连接和官网页面。 " + detail


def synchronize(args):
    records = 0
    errors, notices = [], []
    jobs = [(category, brand) for category, brands in
            (("gpu", ("nvidia", "amd", "intel")), ("cpu", ("amd", "intel")))
            if args.category in (category, "all") for brand in brands if args.brand in (brand, "all")]
    if not jobs:
        errors.append("NVIDIA 暂无 CPU 采集来源")
    for category, brand in jobs:
        label = f"{database.BRANDS[brand]} {category.upper()}"
        started = database.utc_now()
        try:
            if category == "gpu":
                rows = collector.collect_and_save_gpus([brand], headless=not args.show_browser)
                records += len(rows)
                audit = last_gpu_result(brand, started)
                if audit and audit[0] == "partial":
                    notices.append(f"{label} 跳过 {audit[1]} 条")
            else:
                report = {}
                rows = cpu_collector.collect_and_save_cpus(
                    [brand], headless=not args.show_browser, refresh_intel=args.refresh_intel,
                    limit_intel=args.limit_intel, report=report,
                )
                records += len(rows)
                info = report.get(brand, {})
                if info.get("errors"):
                    errors.append(f"{label} 部分采集失败：{info['errors'][0]}")
                elif info.get("cached"):
                    notices.append(f"{label} 已有 {info['cached']} 条")
        except Exception as exc:
            message = error_message(exc)
            if category == "gpu" and "No official data" in str(exc):
                audit = last_gpu_result(brand, started)
                if audit and audit[2]:
                    message += " " + audit[2]
            errors.append(f"{label} {message}")
    if jobs and database.DB_PATH.exists():
        try:
            checkpoint()
        except Exception as exc:
            errors.append(f"数据库检查点失败：{exc}")
    summary = f"本次写入 {records} 条"
    message = "；".join([*errors, summary, *notices])
    return dict(ok=not errors, message=message, records=records, brand=args.brand, category=args.category)


def main(argv=None):
    parser = argparse.ArgumentParser(description="同步 CoreBase 官方硬件数据")
    parser.add_argument("--category", choices=("gpu", "cpu", "all"), default="all")
    parser.add_argument("--brand", choices=("nvidia", "amd", "intel", "all"), default="all")
    parser.add_argument("--limit-intel", type=int)
    parser.add_argument("--refresh-intel", action="store_true")
    parser.add_argument("--show-browser", action="store_true")
    parser.add_argument("--status-json", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.limit_intel is not None and args.limit_intel < 1:
            raise ValueError("--limit-intel 必须是正整数")
        status = synchronize(args)
    except Exception as exc:
        status = dict(ok=False, message=str(exc), records=0, brand=args.brand, category=args.category)
    body = json.dumps(status, ensure_ascii=False)
    if args.status_json:
        try:
            # The UI gives every process a unique private status path.
            args.status_json.write_text(body, encoding="utf-8")
        except OSError as exc:
            print(f"无法写入状态文件：{exc}", file=sys.stderr)
            return 1
    print(body)
    return 0 if status["ok"] else 1


if __name__ == "__main__":
    # A CREATE_NO_WINDOW process has no console and may inherit a legacy code page.
    for name in ("stdout", "stderr"):
        stream = getattr(sys, name)
        if stream is None:
            setattr(sys, name, open(os.devnull, "w", encoding="utf-8"))
        else:
            stream.reconfigure(encoding="utf-8", errors="replace")
    raise SystemExit(main())
