"""Offline CPU parsing and incremental collection regressions."""
import sqlite3
import sys
import tempfile
import unittest
from contextlib import closing
from pathlib import Path
from unittest.mock import MagicMock, patch

import cpu_collector as cpu
import database


def amd_item(name="Ryzen sample", **specs):
    fields = dict(name=name, numOfCpuCores=4, formFactor="Desktops")
    fields.update(specs)
    return {"elements": {key: {"formatValue": value} for key, value in fields.items()}}


class AmdCpuTests(unittest.TestCase):
    def test_zen_technologies_and_explicit_fields(self):
        hsa = "Heterogeneous System Architecture (HSA)"
        for arch in ("AMD Zen Core", "AMD Zen+ (Core)", "AMD Zen 2 Core", "AMD Zen 3 Core"):
            with self.subTest(arch=arch):
                row = cpu.amd_record(amd_item(supportedTechnologies=[hsa, arch + " Architecture"]))
                self.assertEqual(row["architecture"], arch)
        for key in ("cpuCoreArchitecture", "architecture"):
            with self.subTest(key=key):
                self.assertEqual(cpu.amd_record(amd_item(**{key: "Zen 4"},
                    supportedTechnologies=["Zen 3 Architecture"]))["architecture"], "Zen 4")
        for techs in ([hsa], [], ["Zen Architecture", "Zen 2 Architecture"]):
            with self.subTest(techs=techs):
                row = cpu.amd_record(amd_item(formerCodename="Summit Ridge", supportedTechnologies=techs))
                self.assertIsNone(row["architecture"])
        hybrid = amd_item(cpuCoreArchitecture=["Zen 5", "Zen 5c"])
        self.assertEqual(cpu.amd_record(hybrid)["architecture"], "Zen 5, Zen 5c")

    def test_missing_platform_requires_another_valid_record(self):
        legacy = amd_item("FX-8800P")
        del legacy["elements"]["formFactor"]
        with self.assertRaisesRegex(ValueError, "No valid AMD CPU"):
            cpu.parse_amd_snapshot({"items": [legacy]})
        batch = cpu.parse_amd_snapshot({"items": [legacy, amd_item()]})
        self.assertEqual(len(batch.rows), 1)
        self.assertEqual(len(batch.errors), 1)
        self.assertIn("form factor", batch.errors[0])


class IncrementalTests(unittest.TestCase):
    def test_intel_cached_sku_is_not_visited_twice(self):
        url = "https://www.intel.com/content/www/us/en/ark/products/sku/123/example/specifications.html"
        entry = {"name": "Intel Core example", "id": "123", "url": url}
        row = cpu.intel_record(entry["name"], url, [("Vertical Segment", "Desktop"),
            ("Total Cores", "8"), ("Total Threads", "16"), ("Processor Base Power", "65 W")])
        api = MagicMock()
        with tempfile.TemporaryDirectory() as folder, patch.object(database, "DB_PATH", Path(folder) / "hw.db"), \
             patch.dict(sys.modules, {"playwright.sync_api": api}), \
             patch.object(cpu, "intel_series", return_value={"series-url": "Core"}), \
             patch.object(cpu, "intel_directory", return_value=[entry]), \
             patch.object(cpu, "intel_detail", return_value=row) as detail:
            report = {}
            self.assertEqual(cpu.collect_and_save_cpus(["intel"], report=report), [row])
            self.assertEqual(cpu.collect_and_save_cpus(["intel"], report=report), [])
            self.assertEqual(report["intel"], dict(status="success", records=0, errors=[], cached=1))
            detail.assert_called_once()
            launch = api.sync_playwright.return_value.__enter__.return_value.chromium.launch
            self.assertTrue(launch.call_args.kwargs["headless"])
            with closing(sqlite3.connect(database.DB_PATH)) as db:
                self.assertEqual(db.execute("SELECT cores, threads, tdp_w FROM cpus").fetchall(), [(8, 16, 65)])


if __name__ == "__main__":
    unittest.main()
