"""Offline bridge tests. Only temporary databases and status files are written."""
import io
import json
import sqlite3
import tempfile
import unittest
from contextlib import closing, redirect_stdout
from pathlib import Path
from unittest.mock import patch

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
