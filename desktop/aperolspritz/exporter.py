import csv
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable, List, Mapping, Tuple


HEADERS = [
    "Дата", "Время", "Устройство", "IP", "Режим", "Длина, мм",
    "Номинал, мм", "Допуск, мм", "Отклонение, мм", "Результат",
    "Тип калибровки", "Опорная длина, мм", "Measurement position, steps",
    "Calibration position, steps", "mm/step", "Измерений после калибровки",
]


class NoMeasurementsError(ValueError):
    pass


def _export_row(row: Mapping[str, Any]) -> List[Any]:
    timestamp = datetime.fromisoformat(str(row["timestamp_local"]))
    return [
        timestamp.strftime("%d.%m.%Y"), timestamp.strftime("%H:%M:%S"),
        row["device_name"], row["device_ip"], row["mode"], row["length_mm"],
        row["nominal_mm"], row["tolerance_mm"], row["deviation_mm"], row["result"],
        row["calibration_source"], row["calibration_reference_mm"],
        row["measurement_position_steps"], row["calibration_position_steps"],
        row["mm_per_step"], row["measurements_since_calibration"],
    ]


def _materialize(rows: Iterable[Mapping[str, Any]]) -> List[Mapping[str, Any]]:
    result = list(rows)
    if not result:
        raise NoMeasurementsError("Нет измерений для экспорта")
    return result


def export_csv(rows: Iterable[Mapping[str, Any]], path: Path) -> int:
    data = _materialize(rows)
    with Path(path).open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.writer(handle, delimiter=";")
        writer.writerow(HEADERS)
        for row in data:
            writer.writerow(_export_row(row))
    return len(data)


def export_excel(rows: Iterable[Mapping[str, Any]], path: Path) -> int:
    from openpyxl import Workbook
    from openpyxl.styles import Font

    data = _materialize(rows)
    workbook = Workbook()
    sheet = workbook.active
    sheet.title = "Измерения"
    sheet.append(HEADERS)
    for cell in sheet[1]:
        cell.font = Font(bold=True)
    for row in data:
        sheet.append(_export_row(row))
    sheet.freeze_panes = "A2"
    sheet.auto_filter.ref = sheet.dimensions
    widths = [12, 11, 24, 17, 12, 13, 14, 12, 16, 20, 18, 18, 27, 28, 14, 28]
    for index, width in enumerate(widths, start=1):
        sheet.column_dimensions[chr(64 + index)].width = width
    for row in sheet.iter_rows(min_row=2):
        for column in (6, 7, 8, 9, 12):
            row[column - 1].number_format = "0.000"
        row[14].number_format = "0.000000000"
    workbook.save(str(path))
    return len(data)
