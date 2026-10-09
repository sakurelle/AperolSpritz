import json
import os
import re
import uuid
from pathlib import Path
from typing import List, Optional

from .models import Device


HOST_PATTERN = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9.:-]{0,251}[A-Za-z0-9])?$")


def application_data_dir() -> Path:
    base = os.environ.get("APPDATA")
    if base:
        return Path(base) / "AperolSpritz"
    return Path.home() / ".aperolspritz"


def normalize_address(value: str) -> str:
    address = value.strip()
    if address.startswith("http://"):
        address = address[7:]
    if address.startswith("https://"):
        address = address[8:]
    address = address.rstrip("/")
    if not address or "/" in address or not HOST_PATTERN.match(address):
        raise ValueError("Введите IP-адрес или имя устройства без пути")
    return address


class SettingsStore:
    def __init__(self, path: Optional[Path] = None) -> None:
        self.path = path or application_data_dir() / "settings.json"
        self.client_id = ""
        self.devices: List[Device] = []

    def load(self) -> List[Device]:
        self.client_id = str(uuid.uuid4())
        self.devices = []
        try:
            with self.path.open("r", encoding="utf-8") as handle:
                payload = json.load(handle)
            self.client_id = str(payload.get("client_id") or self.client_id)
            self.devices = [Device.from_dict(item) for item in payload.get("devices", [])]
        except (OSError, ValueError, KeyError, TypeError):
            pass
        return list(self.devices)

    def save(self, devices: List[Device]) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if not self.client_id:
            self.client_id = str(uuid.uuid4())
        payload = {"client_id": self.client_id, "devices": [device.to_dict() for device in devices]}
        temporary = self.path.with_suffix(".tmp")
        with temporary.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle, ensure_ascii=False, indent=2)
        temporary.replace(self.path)
        self.devices = list(devices)
