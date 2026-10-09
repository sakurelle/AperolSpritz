import queue
import threading
import time
from typing import Any, Dict, List, Optional, Tuple

import requests

from .models import Device, MeasurementEvent


HTTP_TIMEOUT = (0.8, 1.0)
POLL_INTERVAL_SECONDS = 0.75
HEARTBEAT_INTERVAL_SECONDS = 2.0


class DeviceWorker(threading.Thread):
    def __init__(self, device: Device, client_id: str, events: "queue.Queue[Tuple[Any, ...]]") -> None:
        super().__init__(name="AperolSpritz-{}".format(device.local_id[:8]), daemon=True)
        self.device = device
        self.client_id = client_id
        self.events = events
        self.stop_requested = threading.Event()
        self.session = requests.Session()
        self.last_boot_id: Optional[str] = None
        self.last_seq = -1
        self.last_heartbeat = 0.0

    @property
    def base_url(self) -> str:
        return "http://{}".format(self.device.ip)

    def stop(self) -> None:
        self.stop_requested.set()

    def _get_json(self, path: str) -> Dict[str, Any]:
        response = self.session.get(self.base_url + path, timeout=HTTP_TIMEOUT)
        response.raise_for_status()
        return response.json()

    def _heartbeat(self) -> None:
        response = self.session.post(
            self.base_url + "/api/logger/heartbeat",
            json={"client_id": self.client_id, "client_name": "AperolSpritz Desktop", "version": "1.0"},
            timeout=HTTP_TIMEOUT,
        )
        response.raise_for_status()
        self.last_heartbeat = time.monotonic()

    def _poll_once(self) -> None:
        status = self._get_json("/api/status")
        boot_id = str(status.get("boot_id", ""))
        if boot_id != self.last_boot_id:
            self.last_boot_id = boot_id
            self.last_seq = -1
        history = self._get_json("/api/measurements?after_seq={}".format(self.last_seq))
        history_boot_id = str(history.get("boot_id", ""))
        if history_boot_id != self.last_boot_id:
            self.last_boot_id = history_boot_id
            self.last_seq = -1
            history = self._get_json("/api/measurements?after_seq=-1")
        if bool(history.get("history_gap")):
            self.events.put(("history_gap", self.device.local_id))
        api_events = sorted(history.get("events", []), key=lambda item: int(item.get("measurement_seq", -1)))
        for raw_event in api_events:
            event = MeasurementEvent.from_api(raw_event)
            if event.measurement_seq > self.last_seq:
                self.events.put(("measurement", self.device.local_id, event))
                self.last_seq = event.measurement_seq
        if time.monotonic() - self.last_heartbeat >= HEARTBEAT_INTERVAL_SECONDS:
            self._heartbeat()
        self.events.put(("status", self.device.local_id, status))

    def run(self) -> None:
        reported_offline = False
        while not self.stop_requested.is_set():
            try:
                self._poll_once()
                reported_offline = False
            except (requests.RequestException, ValueError, KeyError, TypeError) as error:
                if not reported_offline:
                    self.events.put(("offline", self.device.local_id, str(error)))
                    reported_offline = True
            self.stop_requested.wait(POLL_INTERVAL_SECONDS)
        self.session.close()


class ConnectionManager:
    def __init__(self, client_id: str, events: "queue.Queue[Tuple[Any, ...]]") -> None:
        self.client_id = client_id
        self.events = events
        self.workers: Dict[str, DeviceWorker] = {}

    def update_devices(self, devices: List[Device]) -> None:
        wanted = {device.local_id: device for device in devices if device.enabled}
        for local_id, worker in list(self.workers.items()):
            replacement = wanted.get(local_id)
            if replacement is None or replacement.ip != worker.device.ip:
                worker.stop()
                worker.join(timeout=2.0)
                del self.workers[local_id]
        for local_id, device in wanted.items():
            if local_id not in self.workers:
                worker = DeviceWorker(device, self.client_id, self.events)
                self.workers[local_id] = worker
                worker.start()
            else:
                self.workers[local_id].device = device

    def close(self) -> None:
        workers = list(self.workers.values())
        for worker in workers:
            worker.stop()
        for worker in workers:
            worker.join(timeout=2.0)
        self.workers.clear()
