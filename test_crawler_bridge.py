"""Offline bridge tests. Only temporary databases and status files are written."""
import io
import json
import sqlite3
import sys
import tempfile
import unittest
from contextlib import closing, redirect_stdout
from pathlib import Path
from unittest.mock import MagicMock, patch

import crawler_bridge as bridge
import database


class BridgeTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.status_file = self.root / "同步结果.json"
        db_path = patch.object(database, "DB_PATH", self.root / "hardware.db")
        db_path.start()
        self.addCleanup(db_path.stop)
        database.init_database()

    def invoke(self, *args):
        with redirect_stdout(io.StringIO()):
            code = bridge.main([*args, "--status-json", str(self.status_file)])
        status = json.loads(self.status_file.read_text(encoding="utf-8"))
        self.assertEqual(status["ok"], code == 0)
        return code, status

    def test_gpu_save_checkpoint_and_status(self):
        def collect(brands, headless):
            self.assertEqual((brands, headless), (["intel"], True))
            rows = [dict(model="Arc B580", vram_gb=12, vram_type="GDDR6", brand="Intel")]
            database.save_gpus(rows, brand="intel")
            return rows

        # Keep WAL open so last-connection auto-checkpoint cannot satisfy this test.
        with closing(database.connect()), patch.object(bridge.collector, "collect_and_save_gpus", collect):
            code, status = self.invoke("--category", "gpu", "--brand", "intel")
            self.assertEqual(code, 0)
            self.assertEqual((status["category"], status["brand"], status["records"]), ("gpu", "intel", 1))
            self.assertEqual(Path(str(database.DB_PATH) + "-wal").stat().st_size, 0)
            with closing(sqlite3.connect(database.DB_PATH.as_uri() + "?immutable=1", uri=True)) as snapshot:
                self.assertEqual(snapshot.execute("SELECT model FROM gpus").fetchone()[0], "Intel Arc B580")

    def test_cpu_options_and_successful_incremental_run(self):
        with patch.object(bridge.cpu_collector, "collect_and_save_cpus", return_value=[]) as collect:
            code, status = self.invoke("--brand", "intel", "--category", "cpu", "--limit-intel", "3",
                                       "--refresh-intel", "--show-browser")
        self.assertEqual(code, 0)
        self.assertEqual(status["records"], 0)
        collect.assert_called_once_with(["intel"], headless=False, refresh_intel=True, limit_intel=3, report={})

    def test_amd_background_sync_parses_and_commits_both_snapshots(self):
        def product(**fields):
            return {"elements": {k: {"formatValue": v} for k, v in fields.items()}}

        api = MagicMock()
        launch = api.sync_playwright.return_value.__enter__.return_value.chromium.launch
        page = launch.return_value.new_page.return_value
        page.evaluate.side_effect = [
            {"summary": "", "payload": {"items": [product(name="RX 9060 XT LP", boardType="Desktop",
                maxMemorySize="16 GB", memoryType="GDDR6", memoryInterface="128-bit",
                typicalBoardPowerDesktop="Up to 140W")]}},
            {"summary": "", "payload": {"items": [
                product(name="Ryzen sample", formFactor="Desktops", numOfCpuCores=4,
                        supportedTechnologies=["AMD Zen 3 Core Architecture"]),
                product(name="FX-8800P", numOfCpuCores=4)]}},
        ]
        with patch.dict(sys.modules, {"playwright.sync_api": api}), \
             patch.object(bridge.collector, "__file__", str(self.root / "collector.py")), \
             patch.object(bridge.collector, "navigate"), patch.object(bridge.cpu_collector, "navigate"):
            code, status = self.invoke("--brand", "amd")
        self.assertEqual((code, status["records"]), (0, 2))
        self.assertIn("部分完成", status["message"])
        self.assertEqual(launch.call_count, 2)
        for call in launch.call_args_list:
            self.assertFalse(call.kwargs["headless"])
            self.assertIn("--window-position=-2400,-2400", call.kwargs["args"])
        with closing(sqlite3.connect(database.DB_PATH.as_uri() + "?immutable=1", uri=True)) as db:
            self.assertEqual(db.execute("SELECT tdp_w FROM gpus").fetchall(), [(140,)])
            self.assertEqual(db.execute("SELECT architecture FROM cpus").fetchall(), [("AMD Zen 3 Core",)])
            self.assertEqual(db.execute("SELECT status FROM sync_logs WHERE category='CPU'").fetchone(),
                             ("partial",))

    def test_partial_with_cached_rows_and_total_failure(self):
        for state, cached, expected in [("partial", 3, 0), ("failed", 0, 1), ("partial", 0, 1)]:
            def collect(brands, *, report, **options):
                report["intel"] = dict(status=state, cached=cached, errors=["detail unavailable"])
                return []

            with self.subTest(state=state, cached=cached), \
                 patch.object(bridge.cpu_collector, "collect_and_save_cpus", collect):
                code, status = self.invoke("--category", "cpu", "--brand", "intel")
                self.assertEqual(code, expected)
                self.assertIn("detail unavailable", status["message"])
                if cached:
                    self.assertIn("已有 3 条", status["message"])

    def test_missing_playwright_and_network_failure(self):
        for error, expected in [(ModuleNotFoundError("No module named 'playwright'"), "pip install playwright"),
                                (TimeoutError("request timed out"), "网络")]:
            with self.subTest(error=error), patch.object(bridge.collector, "collect_and_save_gpus", side_effect=error):
                code, status = self.invoke("--category", "gpu", "--brand", "amd")
                self.assertNotEqual(code, 0)
                self.assertIn(expected, status["message"])

    def test_all_skips_unsupported_cpu_and_keeps_successful_counts(self):
        with patch.object(bridge.collector, "collect_and_save_gpus", return_value=[{}]) as gpu, \
             patch.object(bridge.cpu_collector, "collect_and_save_cpus", side_effect=RuntimeError("官网超时")) as cpu:
            code, status = self.invoke("--category", "all", "--brand", "all")
        self.assertEqual((gpu.call_count, cpu.call_count), (3, 2))
        self.assertEqual(status["records"], 3)
        self.assertNotEqual(code, 0)
        self.assertIn("官网超时", status["message"])

    def test_checkpoint_failure_is_reported_after_save(self):
        with patch.object(bridge.collector, "collect_and_save_gpus", return_value=[{}]), \
             patch.object(bridge, "checkpoint", side_effect=RuntimeError("busy")):
            code, status = self.invoke("--category", "all", "--brand", "nvidia")
        self.assertNotEqual(code, 0)
        self.assertEqual(status["records"], 1)
        self.assertIn("检查点失败", status["message"])

    def test_unsupported_category_and_bad_limit(self):
        for args in [("--category", "cpu", "--brand", "nvidia"), ("--limit-intel", "0")]:
            with self.subTest(args=args):
                code, status = self.invoke(*args)
                self.assertNotEqual(code, 0)
                self.assertTrue(status["message"])


if __name__ == "__main__":
    unittest.main()
