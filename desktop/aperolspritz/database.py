import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from typing import List, Optional

from .models import Device, MeasurementEvent
from .settings import application_data_dir


SCHEMA = """
CREATE TABLE IF NOT EXISTS measurements (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    device_name TEXT NOT NULL,
    device_ip TEXT NOT NULL,
    timestamp_local TEXT NOT NULL,
    timestamp_utc TEXT NOT NULL,
    boot_id TEXT NOT NULL,
    measurement_seq INTEGER NOT NULL,
    mode TEXT NOT NULL,
    length_mm REAL NOT NULL,
    nominal_mm REAL NOT NULL,
    tolerance_mm REAL NOT NULL,
    deviation_mm REAL NOT NULL,
    result TEXT NOT NULL,
    calibration_source TEXT NOT NULL,
    calibration_reference_mm REAL NOT NULL,
    measurement_position_steps INTEGER NOT NULL,
    calibration_position_steps INTEGER NOT NULL,
    mm_per_step REAL NOT NULL,
    measurements_since_calibration INTEGER NOT NULL,
    UNIQUE(device_id, boot_id, measurement_seq)
)
"""


class MeasurementDatabase:
    def __init__(self, path: Optional[Path] = None) -> None:
        self.path = path or application_data_dir() / "measurements.db"
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(str(self.path))
        self.connection.row_factory = sqlite3.Row
        self.connection.execute(SCHEMA)
        self.connection.commit()

    def insert(self, device: Device, event: MeasurementEvent) -> bool:
        local_now = datetime.now().astimezone()
        utc_now = local_now.astimezone(timezone.utc)
        cursor = self.connection.execute(
            """
            INSERT OR IGNORE INTO measurements (
                device_id, device_name, device_ip, timestamp_local, timestamp_utc,
                boot_id, measurement_seq, mode, length_mm, nominal_mm,
                tolerance_mm, deviation_mm, result, calibration_source,
                calibration_reference_mm, measurement_position_steps,
                calibration_position_steps, mm_per_step,
                measurements_since_calibration
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                event.device_id, device.name, device.ip,
                local_now.isoformat(timespec="seconds"), utc_now.isoformat(timespec="seconds"),
                event.boot_id, event.measurement_seq, event.mode, event.length_mm,
                event.nominal_mm, event.tolerance_mm, event.deviation_mm, event.result,
                event.calibration_source, event.calibration_reference_mm,
                event.measurement_position_steps, event.calibration_position_steps,
                event.mm_per_step, event.measurements_since_calibration,
            ),
        )
        self.connection.commit()
        return cursor.rowcount == 1

    def latest(self, limit: int = 200) -> List[sqlite3.Row]:
        return list(self.connection.execute("SELECT * FROM measurements ORDER BY id DESC LIMIT ?", (limit,)))

    def all(self) -> List[sqlite3.Row]:
        return list(self.connection.execute("SELECT * FROM measurements ORDER BY id"))

    def close(self) -> None:
        self.connection.close()
