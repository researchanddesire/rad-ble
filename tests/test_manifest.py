import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads((ROOT / "protocol" / "rad-ble-v1.json").read_text())

    def test_service_uuids_are_unique_rad_uuids(self):
        values = list(self.data["services"].values())
        self.assertEqual(len(values), len(set(values)))
        for value in values:
            self.assertRegex(value, r"^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$")
            self.assertIn("-0001-", value)

    def test_characteristic_suffixes_are_unique(self):
        suffixes = [item["suffix"] for item in self.data["characteristics"].values()]
        self.assertEqual(len(suffixes), len(set(suffixes)))
        for suffix in suffixes:
            self.assertRegex(suffix, r"^[0-9a-f]{4}$")

    def test_required_characteristics_have_no_channel_gate(self):
        for name, item in self.data["characteristics"].items():
            if item["required"]:
                self.assertNotIn("channel", item, name)
            else:
                self.assertIn("channel", item, name)

    def test_operations_and_errors_are_unique(self):
        operations = list(self.data["operations"].values())
        self.assertEqual(len(operations), len(set(operations)))
        self.assertEqual(len(self.data["errors"]), len(set(self.data["errors"])))


if __name__ == "__main__":
    unittest.main()
