"""Offline regression tests. Run: py -m unittest -v test_collector.py"""

import copy
import io
import json
import sys
import tempfile
import types
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import Mock, patch

import collector
import database


# Relevant fields from AMD's actual data-json attribute, observed 2026-11-24 UTC.
# Not used as a fallback by the collector; these are test samples only.
AMD_SAMPLE = json.loads(r'''{"items":[
{"elements":{"name":{"formatValue":"Radeon\u2122 630"},"boardType":{"formatValue":["Desktop","Notebook"],"separator":" , "},"maxMemorySize":{"formatValue":"4","suffix":" GB"},"memoryType":{"formatValue":["GDDR5"],"separator":" / "},"memoryInterface":{"formatValue":"64-bit"}}},
{"elements":{"name":{"formatValue":"Radeon\u2122 RX 580"},"boardType":{"formatValue":["Component"],"separator":" , "},"maxMemorySize":{"formatValue":"8","suffix":" GB"},"memoryType":{"formatValue":["GDDR5"],"separator":" / "},"memoryInterface":{"formatValue":"256-bit"}}},
{"elements":{"name":{"formatValue":"Radeon\u2122 520"},"boardType":{"formatValue":["Desktop","Notebook"],"separator":" , "},"maxMemorySize":{"formatValue":"4","suffix":" GB"},"memoryType":{"formatValue":["GDDR5"],"separator":" / "},"memoryInterface":{"formatValue":"64-bit"}}},
{"elements":{"name":{"formatValue":"AMD Radeon\u2122 RX 7900 XTX"},"boardType":{"formatValue":["Component"],"separator":" , "},"maxMemorySize":{"formatValue":"24","suffix":" GB"},"memoryType":{"formatValue":["GDDR6"],"separator":" / "},"memoryInterface":{}}},
{"elements":{"name":{"formatValue":"AMD Radeon\u2122 RX 9070 XT"},"boardType":{"formatValue":["Desktop"],"separator":" , "},"maxMemorySize":{"formatValue":"16","suffix":" GB"},"memoryType":{"formatValue":["GDDR6"],"separator":" / "},"memoryInterface":{"formatValue":"256-bit"}}},
{"elements":{"name":{"formatValue":"Radeon\u2122 RX Vega 64"},"boardType":{"formatValue":["Desktop","Component"],"separator":" , "},"maxMemorySize":{"formatValue":"8","suffix":" GB"},"memoryType":{"formatValue":["HBM2"],"separator":" / "},"memoryInterface":{"formatValue":"2048-bit"}}}
]}''')


def table(rows):
    return [[{"text": text, "link": None} for text in row] for row in rows]


class AmdTests(unittest.TestCase):
    def test_official_samples_and_missing_specs(self):
        rows, skipped = collector.parse_amd_payload(AMD_SAMPLE, 6)
        actual = {row["model"]: (row["vram_gb"], row["bus_bits"]) for row in rows}
        self.assertEqual(actual, {
            "Radeon RX 580 [8 GB GDDR5]": (8, 256),
            "AMD Radeon RX 9070 XT [16 GB GDDR6]": (16, 256),
            "Radeon RX Vega 64 [8 GB HBM2]": (8, 2048),
        })
        self.assertEqual(len(skipped), 3)
        self.assertTrue(any("7900 XTX" in warning for warning in skipped))
        self.assertTrue(all(row["source_url"] == collector.AMD_URL for row in rows))

    def test_first_display_page_cannot_be_mistaken_for_full_dataset(self):
        with self.assertRaisesRegex(ValueError, "page reports 204"):
            collector.parse_amd_payload(AMD_SAMPLE, expected_total=204)

    def test_changed_schema_and_empty_dataset_fail(self):
        for payload in ({}, {"items": []}, {"items": [{"elements": {}}]}):
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                collector.parse_amd_payload(payload)

    def test_duplicate_product_fails(self):
        payload = copy.deepcopy(AMD_SAMPLE)
        payload["items"].append(payload["items"][4])
        with self.assertRaisesRegex(ValueError, "repeated a product"):
            collector.parse_amd_payload(payload)

    def test_ambiguous_memory_types_are_not_assigned_same_max_capacity(self):
        payload = copy.deepcopy(AMD_SAMPLE)
        payload["items"][4]["elements"]["memoryType"]["formatValue"] = ["DDR3", "GDDR5"]
        rows, skipped = collector.parse_amd_payload(payload)
        self.assertFalse(any("9070" in row["model"] for row in rows))
        self.assertTrue(any("9070" in warning and "ambiguous" in warning for warning in skipped))

    def test_explicit_units_are_preserved(self):
        self.assertEqual(collector.amd_field_values({"formatValue": "16", "suffix": " GB"}), ["16 GB"])
        self.assertEqual(collector.amd_field_values({"formatValue": "16 GB", "suffix": " GB"}), ["16 GB"])
        self.assertEqual(collector.amd_field_values({"value": "256_bit"}), [])

    def test_counter_accepts_early_load_but_rejects_filters(self):
        self.assertIsNone(collector.amd_expected_total(""))
        self.assertEqual(collector.amd_expected_total("Showing 1 to 25 of 204 entries"), 204)
        self.assertEqual(collector.amd_expected_total("Showing 1 to 50 of 1,204 entries"), 1204)
        with self.assertRaises(ValueError):
            collector.amd_expected_total("Showing 1 to 25 of 60 entries (filtered from 204 total entries)")

    def test_collection_uses_embedded_data_without_next_button(self):
        page = Mock(spec=["wait_for_function", "evaluate"])
        page.evaluate.return_value = {"payload": AMD_SAMPLE, "summary": "Showing 1 to 6 of 6 entries"}
        with patch.object(collector, "navigate"):
            rows, _ = collector.collect_amd(page)
        self.assertEqual(len(rows), 3)
        self.assertEqual(page.evaluate.call_args.args[0], collector.AMD_DATA_JS)


class ExistingVendorTests(unittest.TestCase):
    def test_nvidia_capacity_width_pairing(self):
        rows, _ = collector.parse_nvidia([table([
            ["", "GeForce RTX 3080", "GeForce RTX 5060 Ti"],
            ["Standard Memory Config", "12 GB GDDR6X / 10 GB GDDR6X", "16 GB / 8 GB GDDR7"],
            ["Memory Interface Width", "384-bit / 320-bit", "128-bit"],
        ])])
        self.assertEqual([(r["vram_gb"], r["bus_bits"]) for r in rows], [(12, 384), (10, 320), (16, 128), (8, 128)])
        self.assertEqual(len({r["product_id"] for r in rows}), 4)

    def test_intel_separate_and_combined_memory_fields(self):
        for model, sku, fields, size, width in [
            ("A770", "229151", [["Memory Size", "16 GB"], ["Memory Type", "GDDR6"]], 16, 256),
            ("B580", "241598", [["Memory", "12 GB GDDR6"]], 12, 192),
        ]:
            with self.subTest(model=model):
                rows = collector.parse_intel([table([
                    ["Vertical Segment", "Desktop"], ["Model Number", model],
                    ["Graphics Memory Interface", f"{width} bit"], *fields,
                ])], f"https://www.intel.com/content/www/us/en/products/sku/{sku}/gpu/specifications.html")
                self.assertEqual((rows[0]["vram_gb"], rows[0]["bus_bits"]), (size, width))

    def test_nvidia_extended_specs_and_variants(self):
        rows, _ = collector.parse_nvidia([
            table([
                ["", "GeForce RTX 5090", "GeForce RTX 4090"],
                ["NVIDIA CUDA \u00ae Cores", "21760", "16384"],
                ["Shader Cores", "Blackwell", "Ada Lovelace 83 TFLOPS"],
                ["Boost Clock (GHz)", "2.41", "2.52"],
                ["Base Clock (GHz)", "2.01", "2.23"],
                ["Standard Memory Config", "32 GB GDDR7", "24 GB GDDR6X"],
                ["Memory Interface Width", "512-bit", "384-bit"],
                ["Memory Bandwidth", "1792 GB/sec", "1008 GB/sec"],
                ["PCI Express", "Gen 5", "Gen 4"],
                ["Total Graphics Power (W)", "575", "450"],
                ["Required System Power (W) (5)", "1000", "850"],
            ]),
            table([
                ["", "GeForce RTX 3080", "GeForce GTX 1660 Ti"],
                ["NVIDIA CUDA \u00ae Cores", "8960 / 8704", "1536"],
                ["Boost Clock (MHz)", "1710", "1770"],
                ["Base Clock (MHz)", "1260 / 1440", "1500"],
                ["Standard Memory Config", "12 GB GDDR6X / 10 GB GDDR6X", "6GB GDDR6"],
                ["Memory Interface Width", "384-bit / 320-bit", "192-bit"],
                ["NVIDIA Architecture Name", "Ampere", "Turing"],
                ["PCI Express Gen 4", "Yes", "-"],
                ["Graphics Card Power (W)", "350 / 320", "120"],
                ["Minimum System Power (W) (3)", "750", "450"],
            ]),
        ])
        by_id = {r["product_id"]: r for r in rows}
        rtx5090 = by_id["nvidia-nvidia-geforce-rtx-5090-32gb-gddr7"]
        self.assertEqual(rtx5090["architecture"], "Blackwell")
        self.assertEqual(rtx5090["compute_units"], 21760)
        self.assertEqual(rtx5090["compute_unit_type"], "CUDA Cores")
        self.assertEqual((rtx5090["base_clock_mhz"], rtx5090["boost_clock_mhz"]), (2010, 2410))
        self.assertEqual((rtx5090["tdp_w"], rtx5090["recommended_psu_w"]), (575, 1000))
        self.assertEqual((rtx5090["bandwidth_gbps"], rtx5090["pcie_interface"]), (1792.0, "PCIe Gen 5"))

        self.assertEqual(by_id["nvidia-nvidia-geforce-rtx-4090-24gb-gddr6x"]["architecture"], "Ada Lovelace")

        rtx3080_12 = by_id["nvidia-nvidia-geforce-rtx-3080-12gb-gddr6x"]
        rtx3080_10 = by_id["nvidia-nvidia-geforce-rtx-3080-10gb-gddr6x"]
        self.assertEqual((rtx3080_12["compute_units"], rtx3080_12["base_clock_mhz"], rtx3080_12["tdp_w"]), (8960, 1260, 350))
        self.assertEqual((rtx3080_10["compute_units"], rtx3080_10["base_clock_mhz"], rtx3080_10["tdp_w"]), (8704, 1440, 320))
        self.assertEqual(rtx3080_12["pcie_interface"], "PCIe Gen 4")

        gtx1660ti = by_id["nvidia-nvidia-geforce-gtx-1660-ti-6gb-gddr6"]
        self.assertEqual((gtx1660ti["architecture"], gtx1660ti["boost_clock_mhz"], gtx1660ti["tdp_w"]), ("Turing", 1770, 120))

    def test_amd_extended_specs_and_ambiguous_ranges(self):
        payload = {"items": [
            {"elements": {
                "name": {"formatValue": "AMD Radeon\u2122 RX 9070 XT"},
                "boardType": {"formatValue": ["Desktop"]},
                "maxMemorySize": {"formatValue": "16", "suffix": " GB"},
                "memoryType": {"formatValue": ["GDDR6"]},
                "memoryInterface": {"formatValue": "256-bit"},
                "streamProcessors": {"formatValue": ["4096"]},
                "gameFrequency": {"formatValue": "2400 MHz"},
                "boostFrequency": {"formatValue": ["2970 MHz"]},
                "memoryBandwidth": {"formatValue": "640", "suffix": " GB/s"},
                "typicalBoardPowerDesktop": {"formatValue": "304W"},
                "minimumPsuRecommendation": {"formatValue": "750", "suffix": "W"},
                "busType": {"formatValue": ["PCIe\u00ae 5.0 x16"]},
                "launchDate": {"formatValue": "Q1 2025"},
            }},
            {"elements": {
                "name": {"formatValue": "Radeon\u2122 RX 560"},
                "boardType": {"formatValue": ["Desktop"]},
                "maxMemorySize": {"formatValue": "4", "suffix": " GB"},
                "memoryType": {"formatValue": ["GDDR5"]},
                "memoryInterface": {"formatValue": "128-bit"},
                "streamProcessors": {"formatValue": ["896", "1024"]},
                "baseFrequency": {"formatValue": ["1175 MHz"]},
                "boostFrequency": {"formatValue": ["1275 MHz"]},
                "typicalBoardPowerDesktop": {"formatValue": "60-80 W"},
                "supportedTechnologies": {"formatValue": ["4th Gen GCN Architecture", "AMD FreeSync\u2122 Technology"]},
            }},
        ]}
        rows, _ = collector.parse_amd_payload(payload, 2)
        rx9070 = rows[0]
        self.assertEqual(rx9070["compute_units"], 4096)
        self.assertEqual(rx9070["compute_unit_type"], "Stream Processors")
        self.assertEqual((rx9070["base_clock_mhz"], rx9070["boost_clock_mhz"]), (2400, 2970))
        self.assertEqual((rx9070["tdp_w"], rx9070["recommended_psu_w"]), (304, 750))
        self.assertEqual((rx9070["bandwidth_gbps"], rx9070["pcie_interface"], rx9070["release_year"]), (640.0, "PCIe 5.0 x16", 2025))

        rx560 = rows[1]
        self.assertIsNone(rx560["compute_units"])
        self.assertIsNone(rx560["tdp_w"])
        self.assertEqual(rx560["architecture"], "4th Gen GCN")

    def test_intel_extended_specs(self):
        rows = collector.parse_intel([table([
            ["Vertical Segment", "Desktop"],
            ["Model Number", "B580"],
            ["Microarchitecture", "Xe2"],
            ["Launch Date", "Q4'24"],
            ["Xe-cores", "20"],
            ["Graphics Clock", "2670 MHz"],
            ["Total Board Power (TBP)", "190 W"],
            ["Memory", "12 GB GDDR6"],
            ["Graphics Memory Interface", "192 bit"],
            ["Graphics Memory Bandwidth", "456 GB/s"],
            ["PCI Express Configurations", "PCI Express 4.0 x8"],
        ])], "https://www.intel.com/content/www/us/en/products/sku/241598/intel-arc-b580-graphics/specifications.html")
        b580 = rows[0]
        self.assertEqual(b580["architecture"], "Xe2")
        self.assertEqual((b580["compute_units"], b580["compute_unit_type"]), (20, "Xe-Cores"))
        self.assertEqual((b580["base_clock_mhz"], b580["tdp_w"], b580["release_year"]), (2670, 190, 2024))
        self.assertEqual((b580["bandwidth_gbps"], b580["pcie_interface"]), (456.0, "PCI Express 4.0 x8"))

    def test_extended_specs_persist_to_database(self):
        rows = collector.parse_intel([table([
            ["Vertical Segment", "Desktop"],
            ["Model Number", "B580"],
            ["Microarchitecture", "Xe2"],
            ["Xe-cores", "20"],
            ["Graphics Clock", "2670 MHz"],
            ["Total Board Power (TBP)", "190 W"],
            ["Memory", "12 GB GDDR6"],
            ["Graphics Memory Interface", "192 bit"],
        ])], "https://www.intel.com/content/www/us/en/products/sku/241598/intel-arc-b580-graphics/specifications.html")
        with tempfile.TemporaryDirectory() as tmp:
            temp_db = Path(tmp) / "test_hardware.db"
            with patch.object(database, "DB_PATH", temp_db):
                database.save_gpus(rows, brand="intel")
                saved = database.get_gpus()
        self.assertEqual(len(saved), 1)
        self.assertEqual(saved[0]["architecture"], "Xe2")
        self.assertEqual(saved[0]["compute_units"], 20)
        self.assertEqual(saved[0]["compute_unit_type"], "Xe-Cores")
        self.assertEqual(saved[0]["base_clock_mhz"], 2670)
        self.assertEqual(saved[0]["tdp_w"], 190)


class BrowserError(Exception):
    pass


class BrowserTimeout(BrowserError):
    pass


class NavigationTests(unittest.TestCase):
    def setUp(self):
        api = types.ModuleType("playwright.sync_api")
        api.Error, api.TimeoutError = BrowserError, BrowserTimeout
        self.modules = patch.dict(sys.modules, {"playwright.sync_api": api})
        self.modules.start()
        self.addCleanup(self.modules.stop)
        self.sleep = patch.object(collector.time, "sleep")
        self.sleep.start()
        self.addCleanup(self.sleep.stop)
        self.page = Mock(url=collector.AMD_URL)
        self.page.goto.return_value = Mock(status=200)

    def test_navigation_waits_for_commit_not_third_party_scripts(self):
        collector.navigate(self.page, collector.AMD_URL, "amd.com")
        self.assertEqual(self.page.goto.call_args.kwargs["wait_until"], "commit")

    def test_one_transient_retry(self):
        self.page.goto.side_effect = [BrowserTimeout("slow"), Mock(status=200)]
        collector.navigate(self.page, collector.AMD_URL, "amd.com")
        self.assertEqual(self.page.goto.call_count, 2)

    def test_permanent_timeout_is_bounded(self):
        self.page.goto.side_effect = BrowserTimeout("slow")
        with self.assertRaisesRegex(RuntimeError, "Could not open"):
            collector.navigate(self.page, collector.AMD_URL, "amd.com")
        self.assertEqual(self.page.goto.call_count, 2)

    def test_http_denial_is_not_retried_or_treated_as_data(self):
        self.page.goto.return_value = Mock(status=403)
        with self.assertRaisesRegex(RuntimeError, "HTTP 403"):
            collector.navigate(self.page, collector.AMD_URL, "amd.com")
        self.assertEqual(self.page.goto.call_count, 1)

    def test_official_host_required(self):
        self.page.url = "https://amd.com.example.org/"
        with self.assertRaisesRegex(RuntimeError, "Unexpected redirect"):
            collector.navigate(self.page, collector.AMD_URL, "amd.com")


class CommandTests(unittest.TestCase):
    def setUp(self):
        self.db = types.ModuleType("database")
        self.db.DB_PATH = "temporary-test-only.db"
        self.db.init_database, self.db.save_gpus = Mock(), Mock()
        modules = patch.dict(sys.modules, {"database": self.db})
        modules.start()
        self.addCleanup(modules.stop)

    def test_uppercase_brand_and_preview_do_not_write(self):
        rows, _ = collector.parse_amd_payload(AMD_SAMPLE)
        with patch.object(collector, "collect_gpus", return_value=rows) as collect, redirect_stdout(io.StringIO()):
            self.assertEqual(collector.main(["--brand", "AMD", "--show-browser"]), 0)
        collect.assert_called_once_with(["amd"], headless=False)
        self.db.init_database.assert_not_called()
        self.db.save_gpus.assert_not_called()

    def test_save_only_writes_validated_result(self):
        rows, _ = collector.parse_amd_payload(AMD_SAMPLE)
        with patch.object(collector, "collect_gpus", return_value=rows), redirect_stdout(io.StringIO()):
            self.assertEqual(collector.main(["--brand", "amd", "--save"]), 0)
        self.db.save_gpus.assert_called_once_with(rows)

    def test_failed_collection_never_touches_database(self):
        with patch.object(collector, "collect_gpus", side_effect=RuntimeError("No official data")):
            self.assertEqual(collector.main(["--brand", "amd", "--save"]), 1)
        self.db.init_database.assert_not_called()
        self.db.save_gpus.assert_not_called()


if __name__ == "__main__":
    unittest.main()
