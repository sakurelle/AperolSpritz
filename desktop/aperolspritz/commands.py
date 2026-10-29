"""Asynchronous control-plane client for AperolSpritz devices.

Dangerous POST requests are deliberately issued once.  A timeout is reported as
an ambiguous result because the ESP32 may already have accepted the command.
"""

from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
import json
import queue
import socket
from typing import Any, Dict, Mapping, Optional, Tuple
import uuid

import requests

from .models import Device


COMMAND_TIMEOUT = (0.8, 3.0)
STOP_TIMEOUT = (0.4, 0.8)
CONFIG_FIELDS = (
    "nominal_length_mm",
    "tolerance_mm",
    "calibration_length_mm",
    "auto_calibration_length_mm",
    "retract_mm",
    "auto_calibration_interval",
    "measure_dir_inverted",
)


@dataclass(frozen=True)
class CommandTarget:
    local_id: str
    name: str
    ip: str

    @classmethod
    def capture(cls, device: Device) -> "CommandTarget":
        return cls(device.local_id, device.name, device.ip)

    @property
    def base_url(self) -> str:
        return "http://{}".format(self.ip)


class ApiError(RuntimeError):
    def __init__(self, message: str, status_code: Optional[int] = None, ambiguous: bool = False) -> None:
        super().__init__(message)
        self.status_code = status_code
        self.ambiguous = ambiguous


def calculate_mm_per_step(old_value: float, actual_mm: float, commanded_mm: float) -> float:
    old = float(old_value)
    actual = float(actual_mm)
    expected = abs(float(commanded_mm))
    if old <= 0 or actual <= 0 or expected <= 0:
        raise ValueError("Все значения должны быть положительными, а команда — ненулевой")
    return old * actual / expected


class DeviceApi:
    """Synchronous API surface used by worker pools and unit tests."""

    def __init__(self, session_factory=requests.Session) -> None:
        self.session_factory = session_factory

    @staticmethod
    def _response_error(response: requests.Response) -> ApiError:
        detail = response.text.strip()
        try:
            payload = response.json()
            if isinstance(payload, dict) and payload.get("error"):
                detail = str(payload["error"])
        except (ValueError, json.JSONDecodeError):
            pass
        return ApiError(detail or "Устройство отклонило запрос", response.status_code)

    def _request(
        self,
        target: CommandTarget,
        method: str,
        path: str,
        payload: Optional[Mapping[str, Any]] = None,
        timeout: Tuple[float, float] = COMMAND_TIMEOUT,
    ) -> Dict[str, Any]:
        session = self.session_factory()
        try:
            response = session.request(
                method,
                target.base_url + path,
                json=dict(payload) if payload is not None else None,
                timeout=timeout,
            )
            if not response.ok:
                raise self._response_error(response)
            try:
                result = response.json()
            except ValueError as error:
                raise ApiError("Устройство вернуло некорректный JSON") from error
            if not isinstance(result, dict):
                raise ApiError("Устройство вернуло некорректный JSON")
            return result
        except requests.Timeout as error:
            raise ApiError("Истекло время ожидания ответа", ambiguous=method.upper() == "POST") from error
        except (requests.ConnectionError, socket.error) as error:
            raise ApiError("Нет связи с устройством", ambiguous=method.upper() == "POST") from error
        except requests.RequestException as error:
            raise ApiError(str(error) or "Сетевая ошибка", ambiguous=method.upper() == "POST") from error
        finally:
            session.close()

    def measure(self, target: CommandTarget) -> Dict[str, Any]:
        return self._request(target, "POST", "/api/measure")

    def calibrate(self, target: CommandTarget) -> Dict[str, Any]:
        return self._request(target, "POST", "/api/calibrate")

    def auto_start(self, target: CommandTarget) -> Dict[str, Any]:
        return self._request(target, "POST", "/api/auto/start")

    def auto_stop(self, target: CommandTarget) -> Dict[str, Any]:
        return self._request(target, "POST", "/api/auto/stop")

    def stop(self, target: CommandTarget) -> Dict[str, Any]:
        return self._request(target, "POST", "/api/stop", timeout=STOP_TIMEOUT)

    def reset_stop(self, target: CommandTarget) -> Dict[str, Any]:
        return self._request(target, "POST", "/api/reset-stop")

    def reset_error(self, target: CommandTarget) -> Dict[str, Any]:
        return self._request(target, "POST", "/api/reset-error")

    def get_config(self, target: CommandTarget) -> Dict[str, Any]:
        return self._request(target, "GET", "/api/config")

    def save_config(self, target: CommandTarget, config: Mapping[str, Any]) -> Dict[str, Any]:
        if set(config) != set(CONFIG_FIELDS):
            raise ValueError("Требуется полный набор настроек оператора")
        return self._request(target, "POST", "/api/config", config)

    def service_move(self, target: CommandTarget, distance_mm: float) -> Dict[str, Any]:
        return self._request(target, "POST", "/api/service-move", {"distance_mm": float(distance_mm)})

    def set_mm_per_step(self, target: CommandTarget, value: float) -> Dict[str, Any]:
        return self._request(target, "POST", "/api/service/mm-per-step", {"mm_per_step": float(value)})

    def factory_reset(self, target: CommandTarget) -> Dict[str, Any]:
        return self._request(target, "POST", "/api/factory-reset")


class CommandManager:
    """Runs API calls away from Tk and publishes immutable, device-scoped events."""

    def __init__(self, events: "queue.Queue[Tuple[Any, ...]]", api: Optional[DeviceApi] = None) -> None:
        self.events = events
        self.api = api or DeviceApi()
        self.pool = ThreadPoolExecutor(max_workers=6, thread_name_prefix="AperolSpritz-command")
        # STOP must never wait behind a measurement/configuration request.
        self.stop_pool = ThreadPoolExecutor(max_workers=2, thread_name_prefix="AperolSpritz-stop")
        self.closed = False

    def submit(self, device: Device, operation: str, *args: Any) -> str:
        if self.closed:
            raise RuntimeError("Менеджер команд остановлен")
        target = CommandTarget.capture(device)
        request_id = str(uuid.uuid4())
        executor = self.stop_pool if operation == "stop" else self.pool
        executor.submit(self._run, target, operation, request_id, args)
        return request_id

    def _run(self, target: CommandTarget, operation: str, request_id: str, args: Tuple[Any, ...]) -> None:
        try:
            method = getattr(self.api, operation)
            result = method(target, *args)
        except Exception as error:  # converted into a GUI event; never leaks a worker traceback
            api_error = error if isinstance(error, ApiError) else ApiError(str(error))
            self.events.put((
                "command_result", target.local_id, operation, request_id, False, None,
                str(api_error), api_error.status_code, api_error.ambiguous, target.ip,
            ))
        else:
            self.events.put((
                "command_result", target.local_id, operation, request_id, True, result,
                "", None, False, target.ip,
            ))

    def close(self) -> None:
        self.closed = True
        self.pool.shutdown(wait=False, cancel_futures=True)
        self.stop_pool.shutdown(wait=False, cancel_futures=True)
