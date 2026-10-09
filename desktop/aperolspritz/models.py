from dataclasses import asdict, dataclass
from typing import Any, Dict


@dataclass
class Device:
    local_id: str
    name: str
    ip: str
    enabled: bool = True

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, value: Dict[str, Any]) -> "Device":
        return cls(
            local_id=str(value["local_id"]),
            name=str(value["name"]),
            ip=str(value["ip"]),
            enabled=bool(value.get("enabled", True)),
        )


@dataclass
class MeasurementEvent:
    device_id: str
    boot_id: str
    measurement_seq: int
    mode: str
    length_mm: float
    nominal_mm: float
    tolerance_mm: float
    deviation_mm: float
    result: str
    calibration_source: str
    calibration_reference_mm: float
    measurement_position_steps: int
    calibration_position_steps: int
    mm_per_step: float
    measurements_since_calibration: int

    @classmethod
    def from_api(cls, value: Dict[str, Any]) -> "MeasurementEvent":
        return cls(
            device_id=str(value["device_id"]),
            boot_id=str(value["boot_id"]),
            measurement_seq=int(value["measurement_seq"]),
            mode=str(value.get("mode", "MANUAL")),
            length_mm=float(value["length_mm"]),
            nominal_mm=float(value["nominal_mm"]),
            tolerance_mm=float(value["tolerance_mm"]),
            deviation_mm=float(value["deviation_mm"]),
            result=str(value.get("result", "NONE")),
            calibration_source=str(value.get("calibration_source", "NONE")),
            calibration_reference_mm=float(value.get("calibration_reference_mm", 0.0)),
            measurement_position_steps=int(value.get("measurement_position_steps", 0)),
            calibration_position_steps=int(value.get("calibration_position_steps", 0)),
            mm_per_step=float(value.get("mm_per_step", 0.0)),
            measurements_since_calibration=int(value.get("measurements_since_calibration", 0)),
        )


def result_status(deviation_mm: float, tolerance_mm: float) -> str:
    tolerance = max(0.0, float(tolerance_mm))
    deviation = abs(float(deviation_mm))
    if deviation > tolerance:
        return "OUT_OF_TOLERANCE"
    if tolerance > 0.0 and deviation + 1e-12 >= tolerance * 0.8:
        return "NEAR_LIMIT"
    return "OK"
