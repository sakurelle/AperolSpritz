import queue
import threading
import time
from unittest.mock import Mock, patch

import pytest
import requests

from aperolspritz.commands import (
    ApiError,
    COMMAND_TIMEOUT,
    CONFIG_FIELDS,
    STOP_TIMEOUT,
    CommandManager,
    CommandTarget,
    DeviceApi,
    calculate_mm_per_step,
)
from aperolspritz.models import Device
from aperolspritz.api import ConnectionManager
from aperolspritz.ui import MonitorWindow


class FakeResponse:
    def __init__(self, status=200, payload=None, text=""):
        self.status_code = status
        self._payload = {"accepted": True} if payload is None else payload
        self.text = text
        self.ok = 200 <= status < 300

    def json(self):
        if isinstance(self._payload, Exception):
            raise self._payload
        return self._payload


class FakeSession:
    def __init__(self, response=None, error=None):
        self.response = response or FakeResponse()
        self.error = error
        self.calls = []
        self.closed = False

    def request(self, method, url, json=None, timeout=None):
        self.calls.append((method, url, json, timeout))
        if self.error:
            raise self.error
        return self.response

    def close(self):
        self.closed = True


@pytest.fixture
def target():
    return CommandTarget("local-1", "Установка 1", "10.0.8.100")


@pytest.mark.parametrize(
    "method_name,path",
    [
        ("measure", "/api/measure"), ("calibrate", "/api/calibrate"),
        ("auto_start", "/api/auto/start"), ("auto_stop", "/api/auto/stop"),
        ("stop", "/api/stop"), ("reset_stop", "/api/reset-stop"),
        ("reset_error", "/api/reset-error"), ("factory_reset", "/api/factory-reset"),
    ],
)
def test_command_endpoints(target, method_name, path):
    session = FakeSession()
    api = DeviceApi(lambda: session)
    getattr(api, method_name)(target)
    method, url, payload, timeout = session.calls[0]
    assert method == "POST"
    assert url == "http://10.0.8.100" + path
    assert payload is None
    assert timeout == (STOP_TIMEOUT if method_name == "stop" else COMMAND_TIMEOUT)
    assert session.closed


def test_get_and_save_config_use_exact_contract(target):
    session = FakeSession(FakeResponse(payload={"nominal_length_mm": 180.0}))
    api = DeviceApi(lambda: session)
    assert api.get_config(target)["nominal_length_mm"] == 180.0
    config = {
        "nominal_length_mm": 180.0, "tolerance_mm": 0.1,
        "calibration_length_mm": 41.3, "auto_calibration_length_mm": 170.0,
        "retract_mm": 2.0, "auto_calibration_interval": 10,
        "measure_dir_inverted": False,
    }
    api.save_config(target, config)
    assert session.calls[0][:3] == ("GET", "http://10.0.8.100/api/config", None)
    assert session.calls[1][:3] == ("POST", "http://10.0.8.100/api/config", config)
    assert set(config) == set(CONFIG_FIELDS)
    assert "mm_per_step" not in config


def test_save_config_rejects_partial_payload_without_http(target):
    session = FakeSession()
    api = DeviceApi(lambda: session)
    with pytest.raises(ValueError, match="полный набор"):
        api.save_config(target, {"nominal_length_mm": 180.0})
    assert session.calls == []


def test_service_payloads(target):
    session = FakeSession()
    api = DeviceApi(lambda: session)
    api.service_move(target, -100.0)
    api.set_mm_per_step(target, 0.000311824)
    assert session.calls[0][:3] == (
        "POST", "http://10.0.8.100/api/service-move", {"distance_mm": -100.0},
    )
    assert session.calls[1][:3] == (
        "POST", "http://10.0.8.100/api/service/mm-per-step", {"mm_per_step": 0.000311824},
    )


@pytest.mark.parametrize("status", [400, 409, 500, 503])
def test_http_errors_keep_status_and_server_message(target, status):
    session = FakeSession(FakeResponse(status=status, payload={"error": "Controller busy"}, text="fallback"))
    with pytest.raises(ApiError) as caught:
        DeviceApi(lambda: session).measure(target)
    assert caught.value.status_code == status
    assert str(caught.value) == "Controller busy"
    assert not caught.value.ambiguous


def test_invalid_json_is_reported(target):
    session = FakeSession(FakeResponse(payload=ValueError("bad json")))
    with pytest.raises(ApiError, match="некорректный JSON"):
        DeviceApi(lambda: session).measure(target)


def test_timeout_is_ambiguous_and_never_retried(target):
    session = FakeSession(error=requests.Timeout("late"))
    with pytest.raises(ApiError) as caught:
        DeviceApi(lambda: session).calibrate(target)
    assert caught.value.ambiguous
    assert len(session.calls) == 1


def test_get_timeout_is_not_ambiguous(target):
    session = FakeSession(error=requests.Timeout("late"))
    with pytest.raises(ApiError) as caught:
        DeviceApi(lambda: session).get_config(target)
    assert not caught.value.ambiguous
    assert len(session.calls) == 1


def test_mm_per_step_matches_web_ui_formula():
    assert calculate_mm_per_step(0.000312700, 99.720, -100.0) == pytest.approx(0.0003118244)
    for values in ((0, 1, 1), (1, 0, 1), (1, 1, 0), (1, -1, 1)):
        with pytest.raises(ValueError):
            calculate_mm_per_step(*values)


def test_target_is_captured_before_device_changes():
    device = Device("local", "Установка", "10.0.8.100")
    captured = CommandTarget.capture(device)
    device.ip = "10.0.8.101"
    assert captured.ip == "10.0.8.100"


def test_result_event_remains_scoped_to_original_device():
    events = queue.Queue()
    api = Mock()
    api.calibrate.return_value = {"accepted": True}
    manager = CommandManager(events, api=api)
    first = Device("one", "Первая", "10.0.8.100")
    manager.submit(first, "calibrate")
    event = events.get(timeout=2)
    manager.close()
    assert event[0] == "command_result"
    assert event[1] == "one"
    assert event[9] == "10.0.8.100"


def test_stop_uses_priority_executor_while_normal_pool_is_blocked():
    events = queue.Queue()
    release = threading.Event()
    stop_seen = threading.Event()

    class BlockingApi:
        def measure(self, target):
            release.wait(2)
            return {"accepted": True}

        def stop(self, target):
            stop_seen.set()
            return {"accepted": True}

    manager = CommandManager(events, api=BlockingApi())
    device = Device("one", "Первая", "10.0.8.100")
    for _ in range(6):
        manager.submit(device, "measure")
    manager.submit(device, "stop")
    assert stop_seen.wait(0.5)
    release.set()
    manager.close()


def test_connection_manager_keeps_all_enabled_devices_monitored():
    created = []

    class FakeWorker:
        def __init__(self, device, client_id, events):
            self.device = device
            self.started = self.stopped = False
            created.append(self)

        def start(self):
            self.started = True

        def stop(self):
            self.stopped = True

        def join(self, timeout=None):
            pass

    devices = [Device(str(i), "Установка {}".format(i), "10.0.8.{}".format(100 + i)) for i in range(3)]
    with patch("aperolspritz.api.DeviceWorker", FakeWorker):
        manager = ConnectionManager("client", queue.Queue())
        manager.update_devices(devices)
        assert set(manager.workers) == {"0", "1", "2"}
        assert all(worker.started for worker in created)
        manager.close()
        assert all(worker.stopped for worker in created)


def test_dirty_configuration_blocks_switch_until_save_finishes():
    window = object.__new__(MonitorWindow)
    window.config_dirty = True
    window.root = object()
    window._save_config = Mock()
    with patch("aperolspritz.ui.messagebox.askyesnocancel", return_value=True):
        assert not window._confirm_config_transition()
    window._save_config.assert_called_once_with()


def test_factory_reset_requires_two_confirmations():
    window = object.__new__(MonitorWindow)
    window.root = object()
    window._device = Mock(return_value=Device("one", "Установка", "10.0.8.100"))
    window._send = Mock()
    with patch("aperolspritz.ui.messagebox.askyesno", side_effect=[True, False]):
        window._factory_reset()
    window._send.assert_not_called()
    with patch("aperolspritz.ui.messagebox.askyesno", side_effect=[True, True]):
        window._factory_reset()
    window._send.assert_called_once_with("factory_reset")
