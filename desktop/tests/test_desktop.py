import csv
import os
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

from openpyxl import load_workbook

from aperolspritz.database import MeasurementDatabase
from aperolspritz.exporter import export_csv, export_excel
from aperolspritz.models import Device, MeasurementEvent, result_status
from aperolspritz.settings import SettingsStore, application_data_dir


def sample_event(sequence: int = 1) -> MeasurementEvent:
    return MeasurementEvent(
        device_id="AA:BB:CC:DD:EE:FF", boot_id="0123456789abcdef",
        measurement_seq=sequence, mode="AUTO", length_mm=179.984,
        nominal_mm=180.0, tolerance_mm=0.1, deviation_mm=-0.016,
        result="OK", calibration_source="AUTO", calibration_reference_mm=178.0,
        measurement_position_steps=1234, calibration_position_steps=5678,
        mm_per_step=0.0003127, measurements_since_calibration=3,
    )


class DatabaseTests(unittest.TestCase):
    def test_insert_and_deduplicate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            database = MeasurementDatabase(Path(temporary) / "measurements.db")
            device = Device("local", "Установка 1", "192.168.1.101")
            self.assertTrue(database.insert(device, sample_event()))
            self.assertFalse(database.insert(device, sample_event()))
            self.assertEqual(1, len(database.all()))
            database.close()


class ResultTests(unittest.TestCase):
    def test_result_status(self) -> None:
        self.assertEqual("OK", result_status(0.079, 0.1))
        # Match the Web UI's direct IEEE-754 comparison without pre-rounding.
        self.assertEqual("OK", result_status(0.08, 0.1))
        self.assertEqual("NEAR_LIMIT", result_status(0.080001, 0.1))
        self.assertEqual("OUT_OF_TOLERANCE", result_status(-0.101, 0.1))


class SettingsTests(unittest.TestCase):
    def test_application_data_dir_uses_appdata(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with patch.dict(os.environ, {"APPDATA": temporary}):
                self.assertEqual(Path(temporary) / "AperolSpritz", application_data_dir())

    def test_save_and_load(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "settings.json"
            store = SettingsStore(path)
            store.load()
            original_client_id = store.client_id
            devices = [Device("one", "Установка 1", "192.168.1.101")]
            store.save(devices)
            loaded = SettingsStore(path)
            self.assertEqual(devices, loaded.load())
            self.assertEqual(original_client_id, loaded.client_id)


class ExportTests(unittest.TestCase):
    def _rows(self, temporary: str):
        database = MeasurementDatabase(Path(temporary) / "measurements.db")
        database.insert(Device("local", "Установка 1", "192.168.1.101"), sample_event())
        return database, database.all()

    def test_csv_export_is_bom_and_semicolon(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            database, rows = self._rows(temporary)
            path = Path(temporary) / "measurements.csv"
            self.assertEqual(1, export_csv(rows, path))
            self.assertTrue(path.read_bytes().startswith(b"\xef\xbb\xbf"))
            with path.open("r", encoding="utf-8-sig", newline="") as handle:
                parsed = list(csv.reader(handle, delimiter=";"))
            self.assertEqual("Дата", parsed[0][0])
            self.assertEqual("Установка 1", parsed[1][2])
            database.close()

    def test_excel_export_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            database, rows = self._rows(temporary)
            path = Path(temporary) / "measurements.xlsx"
            self.assertEqual(1, export_excel(rows, path))
            workbook = load_workbook(path)
            sheet = workbook["Измерения"]
            self.assertEqual("A2", sheet.freeze_panes)
            self.assertEqual("Дата", sheet["A1"].value)
            self.assertTrue(sheet["A1"].font.bold)
            self.assertIsNotNone(sheet.auto_filter.ref)
            database.close()


if __name__ == "__main__":
    unittest.main()
