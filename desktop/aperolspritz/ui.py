import queue
import uuid
from pathlib import Path
from tkinter import StringVar, Tk, filedialog, messagebox, simpledialog, ttk
from typing import Any, Dict, List, Optional, Tuple

from .api import ConnectionManager
from .database import MeasurementDatabase
from .exporter import NoMeasurementsError, export_csv, export_excel
from .models import Device, MeasurementEvent, result_status
from .settings import SettingsStore, normalize_address


STATE_NAMES = {
    "IDLE": "Ожидание", "MEASURE_FAST": "Быстрый проход", "MEASURE_RETRACT": "Отъезд",
    "MEASURE_FINE": "Точное измерение", "MEASURE_FINAL_RETRACT": "Финальный отъезд",
    "CALIBRATION_PREPARE": "Предупреждение перед калибровкой",
    "AUTO_CALIBRATION_PREPARE": "Предупреждение перед автокалибровкой",
    "CALIBRATE_FAST": "Калибровка: быстрый проход", "CALIBRATE_RETRACT": "Калибровка: отъезд",
    "CALIBRATE_FINE": "Калибровка: точное касание", "CALIBRATE_FINAL_RETRACT": "Финальный отъезд",
    "AUTO_WAIT_SENSOR_CLEAR": "Ожидание освобождения датчика", "AUTO_WAIT_NEEDLE": "Ожидание иглы",
    "AUTO_WAIT_REMOVE": "Ожидание снятия иглы", "AUTO_MOVE_TO_LOAD": "Возврат в загрузочную позицию",
    "SERVICE_MOVE": "Сервисное перемещение", "FINISHED": "Завершено", "STOPPED": "STOP", "ERROR": "Ошибка",
}

RESULT_NAMES = {"OK": "НОРМА", "NEAR_LIMIT": "БЛИЗКО К ГРАНИЦЕ", "OUT_OF_TOLERANCE": "ВНЕ ДОПУСКА"}


class MonitorWindow:
    def __init__(self, root: Tk, settings: SettingsStore, database: MeasurementDatabase) -> None:
        self.root = root
        self.settings = settings
        self.database = database
        self.devices = settings.load()
        self.events: "queue.Queue[Tuple[Any, ...]]" = queue.Queue()
        self.manager = ConnectionManager(settings.client_id, self.events)
        self.statuses: Dict[str, Dict[str, Any]] = {}
        self.online: Dict[str, bool] = {}
        self.current_device_id: Optional[str] = self.devices[0].local_id if self.devices else None
        self.closing = False

        self.device_var = StringVar()
        self.name_var = StringVar()
        self.ip_var = StringVar()
        self.connection_var = StringVar(value="● Нет выбранного устройства")
        self.length_var = StringVar(value="—")
        self.deviation_var = StringVar(value="Отклонение: —")
        self.nominal_var = StringVar(value="Номинал: —")
        self.tolerance_var = StringVar(value="Допуск: —")
        self.result_var = StringVar(value="Нет измерения")
        self.fsm_var = StringVar(value="FSM: —")
        self.auto_var = StringVar(value="AUTO: —")
        self.sensors_var = StringVar(value="Игла: —    CONTACT: —    STOP: —")
        self.error_var = StringVar(value="Ошибка: —")
        self.calibration_var = StringVar(value="Калибровка: —")
        self.autocal_var = StringVar(value="Автокалибровка: —")
        self.warning_var = StringVar(value="")

        self._build()
        self._refresh_device_choices()
        self._load_history()
        self.manager.update_devices(self.devices)
        self.root.after(100, self._drain_events)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _build(self) -> None:
        self.root.title("AperolSpritz Monitor")
        self.root.geometry("980x780")
        self.root.minsize(820, 650)
        style = ttk.Style(self.root)
        style.configure("Result.TLabel", font=("Segoe UI", 34, "bold"))
        style.configure("ResultState.TLabel", font=("Segoe UI", 15, "bold"))
        style.configure("Online.TLabel", foreground="#18864b")
        style.configure("Offline.TLabel", foreground="#a13a42")
        style.configure("Ok.ResultState.TLabel", foreground="#18864b")
        style.configure("Near.ResultState.TLabel", foreground="#c98a12")
        style.configure("Bad.ResultState.TLabel", foreground="#bd2d35")

        outer = ttk.Frame(self.root, padding=14)
        outer.pack(fill="both", expand=True)
        device_box = ttk.LabelFrame(outer, text="Устройства", padding=10)
        device_box.pack(fill="x")
        device_box.columnconfigure(1, weight=1)
        ttk.Label(device_box, text="Устройство:").grid(row=0, column=0, sticky="w", padx=(0, 8))
        self.device_combo = ttk.Combobox(device_box, textvariable=self.device_var, state="readonly")
        self.device_combo.grid(row=0, column=1, sticky="ew")
        self.device_combo.bind("<<ComboboxSelected>>", self._select_combo)
        self.connection_label = ttk.Label(device_box, textvariable=self.connection_var, style="Offline.TLabel")
        self.connection_label.grid(row=0, column=2, sticky="e", padx=(12, 0))
        ttk.Label(device_box, text="Имя:").grid(row=1, column=0, sticky="w", pady=(8, 0))
        ttk.Entry(device_box, textvariable=self.name_var).grid(row=1, column=1, sticky="ew", pady=(8, 0))
        ttk.Label(device_box, text="IP:").grid(row=2, column=0, sticky="w", pady=(6, 0))
        ttk.Entry(device_box, textvariable=self.ip_var).grid(row=2, column=1, sticky="ew", pady=(6, 0))
        buttons = ttk.Frame(device_box)
        buttons.grid(row=1, column=2, rowspan=2, padx=(12, 0))
        ttk.Button(buttons, text="Добавить устройство", command=self._add_device).pack(fill="x")
        ttk.Button(buttons, text="Сохранить", command=self._save_device).pack(fill="x", pady=4)
        ttk.Button(buttons, text="Удалить", command=self._delete_device).pack(fill="x")

        dashboard = ttk.Frame(outer)
        dashboard.pack(fill="x", pady=12)
        dashboard.columnconfigure(0, weight=3)
        dashboard.columnconfigure(1, weight=2)
        result = ttk.LabelFrame(dashboard, text="Последнее измерение", padding=16)
        result.grid(row=0, column=0, sticky="nsew", padx=(0, 6))
        self.length_label = ttk.Label(result, textvariable=self.length_var, style="Result.TLabel", anchor="center")
        self.length_label.pack(fill="x", pady=(8, 12))
        ttk.Label(result, textvariable=self.deviation_var, anchor="center").pack(fill="x")
        ttk.Label(result, textvariable=self.nominal_var, anchor="center").pack(fill="x")
        ttk.Label(result, textvariable=self.tolerance_var, anchor="center").pack(fill="x")
        self.result_label = ttk.Label(result, textvariable=self.result_var, style="ResultState.TLabel", anchor="center")
        self.result_label.pack(fill="x", pady=(12, 0))

        state = ttk.LabelFrame(dashboard, text="Состояние", padding=14)
        state.grid(row=0, column=1, sticky="nsew", padx=(6, 0))
        for variable in (self.fsm_var, self.auto_var, self.sensors_var, self.error_var, self.calibration_var, self.autocal_var):
            ttk.Label(state, textvariable=variable, wraplength=390).pack(fill="x", anchor="w", pady=3)
        ttk.Label(state, textvariable=self.warning_var, foreground="#bd2d35", wraplength=390).pack(fill="x", anchor="w", pady=(8, 0))

        history = ttk.LabelFrame(outer, text="Последние измерения", padding=8)
        history.pack(fill="both", expand=True)
        columns = ("time", "device", "mode", "length", "deviation", "result", "calibration")
        self.tree = ttk.Treeview(history, columns=columns, show="headings", height=12)
        headings = ("Дата/время", "Устройство", "Режим", "Длина", "Отклонение", "Результат", "Калибровка")
        widths = (145, 170, 75, 90, 100, 130, 100)
        for column, heading, width in zip(columns, headings, widths):
            self.tree.heading(column, text=heading)
            self.tree.column(column, width=width, anchor="center")
        self.tree.tag_configure("ok", foreground="#18864b")
        self.tree.tag_configure("near", foreground="#a86f00")
        self.tree.tag_configure("bad", foreground="#bd2d35")
        scrollbar = ttk.Scrollbar(history, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        self.tree.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        actions = ttk.Frame(outer)
        actions.pack(fill="x", pady=(10, 0))
        ttk.Button(actions, text="Экспорт Excel", command=self._export_excel).pack(side="left")
        ttk.Button(actions, text="Экспорт CSV", command=self._export_csv).pack(side="left", padx=8)
        ttk.Label(actions, text="Данные автоматически сохраняются в SQLite").pack(side="right")

    def _device(self, local_id: Optional[str] = None) -> Optional[Device]:
        wanted = local_id or self.current_device_id
        return next((item for item in self.devices if item.local_id == wanted), None)

    def _refresh_device_choices(self) -> None:
        values = ["{} — {}".format(device.name, device.ip) for device in self.devices]
        self.device_combo["values"] = values
        current = self._device()
        if current:
            index = self.devices.index(current)
            self.device_combo.current(index)
            self.name_var.set(current.name)
            self.ip_var.set(current.ip)
        else:
            self.device_var.set("")
            self.name_var.set("")
            self.ip_var.set("")
            self._show_offline("Нет выбранного устройства")

    def _select_combo(self, _event: Any = None) -> None:
        index = self.device_combo.current()
        if index < 0 or index >= len(self.devices):
            return
        device = self.devices[index]
        self.current_device_id = device.local_id
        self.name_var.set(device.name)
        self.ip_var.set(device.ip)
        status = self.statuses.get(device.local_id)
        if status and self.online.get(device.local_id):
            self._show_status(status)
        else:
            self._show_offline("Нет связи")

    def _add_device(self) -> None:
        name = simpledialog.askstring("Новое устройство", "Понятное имя:", parent=self.root)
        if not name:
            return
        address = simpledialog.askstring("Новое устройство", "IP-адрес:", parent=self.root)
        if not address:
            return
        try:
            normalized = normalize_address(address)
        except ValueError as error:
            messagebox.showerror("Некорректный адрес", str(error), parent=self.root)
            return
        device = Device(str(uuid.uuid4()), name.strip(), normalized)
        self.devices.append(device)
        self.current_device_id = device.local_id
        self._persist_devices()

    def _save_device(self) -> None:
        device = self._device()
        if not device:
            return
        name = self.name_var.get().strip()
        if not name:
            messagebox.showerror("Некорректное имя", "Введите имя устройства", parent=self.root)
            return
        try:
            address = normalize_address(self.ip_var.get())
        except ValueError as error:
            messagebox.showerror("Некорректный адрес", str(error), parent=self.root)
            return
        device.name = name
        device.ip = address
        self._persist_devices()

    def _delete_device(self) -> None:
        device = self._device()
        if not device or not messagebox.askyesno("Удаление", "Удалить устройство «{}»?".format(device.name), parent=self.root):
            return
        self.devices = [item for item in self.devices if item.local_id != device.local_id]
        self.statuses.pop(device.local_id, None)
        self.online.pop(device.local_id, None)
        self.current_device_id = self.devices[0].local_id if self.devices else None
        self._persist_devices()

    def _persist_devices(self) -> None:
        try:
            self.settings.save(self.devices)
        except OSError as error:
            messagebox.showerror("Ошибка сохранения", str(error), parent=self.root)
            return
        self._refresh_device_choices()
        self.manager.update_devices(self.devices)

    def _show_offline(self, text: str) -> None:
        self.connection_var.set("● {}".format(text))
        self.connection_label.configure(style="Offline.TLabel")

    def _show_status(self, status: Dict[str, Any]) -> None:
        device = self._device()
        if not device:
            return
        self.connection_var.set("● Подключено — {}".format(device.ip))
        self.connection_label.configure(style="Online.TLabel")
        has_measurement = status.get("result") not in (None, "", "NONE")
        if has_measurement:
            length = float(status.get("last_length_mm", 0.0))
            deviation = float(status.get("deviation_mm", 0.0))
            nominal = float(status.get("nominal_length_mm", 0.0))
            tolerance = float(status.get("tolerance_mm", 0.0))
            result = result_status(deviation, tolerance)
            self.length_var.set("{:.3f} мм".format(length))
            self.deviation_var.set("Отклонение: {:+.3f} мм".format(deviation))
            self.nominal_var.set("Номинал: {:.3f} мм".format(nominal))
            self.tolerance_var.set("Допуск: ±{:.3f} мм".format(tolerance))
            self.result_var.set(RESULT_NAMES[result])
            self.result_label.configure(style={"OK": "Ok.ResultState.TLabel", "NEAR_LIMIT": "Near.ResultState.TLabel", "OUT_OF_TOLERANCE": "Bad.ResultState.TLabel"}[result])
        else:
            self.length_var.set("—")
            self.result_var.set("Нет измерения")
            self.result_label.configure(style="ResultState.TLabel")
        self.fsm_var.set("FSM: {}".format(STATE_NAMES.get(str(status.get("state")), status.get("state", "—"))))
        auto_text = "выключение…" if status.get("auto_stop_requested") else ("ВКЛ" if status.get("auto_mode_enabled") else "ВЫКЛ")
        self.auto_var.set("AUTO: {}".format(auto_text))
        self.sensors_var.set("Игла: {}    CONTACT: {}    STOP: {}".format(
            "установлена" if status.get("needle_present") else "отсутствует",
            "активен" if status.get("contact") else "свободен",
            "активен" if status.get("stop_active") else "OK",
        ))
        error = str(status.get("error", "NONE"))
        self.error_var.set("Ошибка: {}".format("нет" if error == "NONE" else error))
        source = str(status.get("calibration_source", "NONE"))
        reference = float(status.get("calibration_reference_length_mm", 0.0))
        self.calibration_var.set("Калибровка: {} / {:.3f} мм".format(source, reference) if status.get("calibration_valid") else "Калибровка: нет")
        interval = int(status.get("auto_calibration_interval", 1))
        count = int(status.get("auto_measurements_since_calibration", 0))
        remaining = int(status.get("auto_calibration_remaining", max(0, interval - count)))
        self.autocal_var.set("Автокалибровка: каждые {}  |  После калибровки: {}  |  До следующей: {}".format(interval, count, remaining))

    def _drain_events(self) -> None:
        if self.closing:
            return
        try:
            while True:
                event = self.events.get_nowait()
                kind, local_id = event[0], event[1]
                device = self._device(local_id)
                if not device:
                    continue
                if kind == "status":
                    self.statuses[local_id] = event[2]
                    self.online[local_id] = True
                    if local_id == self.current_device_id:
                        self._show_status(event[2])
                elif kind == "offline":
                    self.online[local_id] = False
                    if local_id == self.current_device_id:
                        self._show_offline("Нет связи")
                elif kind == "history_gap":
                    if local_id == self.current_device_id:
                        self.warning_var.set("Часть измерений могла быть пропущена")
                elif kind == "measurement":
                    measurement = event[2]
                    if self.database.insert(device, measurement):
                        self._insert_measurement(device, measurement)
        except queue.Empty:
            pass
        self.root.after(100, self._drain_events)

    def _insert_measurement(self, device: Device, event: MeasurementEvent) -> None:
        status = result_status(event.deviation_mm, event.tolerance_mm)
        tag = {"OK": "ok", "NEAR_LIMIT": "near", "OUT_OF_TOLERANCE": "bad"}[status]
        from datetime import datetime
        timestamp = datetime.now().astimezone().strftime("%d.%m.%Y %H:%M:%S")
        values = (timestamp, device.name, event.mode, "{:.3f}".format(event.length_mm), "{:+.3f}".format(event.deviation_mm), RESULT_NAMES[status], event.calibration_source)
        self.tree.insert("", 0, values=values, tags=(tag,))
        children = self.tree.get_children()
        if len(children) > 200:
            self.tree.delete(children[-1])

    def _load_history(self) -> None:
        for row in reversed(self.database.latest(200)):
            status = result_status(row["deviation_mm"], row["tolerance_mm"])
            tag = {"OK": "ok", "NEAR_LIMIT": "near", "OUT_OF_TOLERANCE": "bad"}[status]
            timestamp = str(row["timestamp_local"]).replace("T", " ")[:19]
            values = (timestamp, row["device_name"], row["mode"], "{:.3f}".format(row["length_mm"]), "{:+.3f}".format(row["deviation_mm"]), RESULT_NAMES[status], row["calibration_source"])
            self.tree.insert("", 0, values=values, tags=(tag,))

    def _export_csv(self) -> None:
        path = filedialog.asksaveasfilename(parent=self.root, title="Экспорт CSV", defaultextension=".csv", filetypes=[("CSV", "*.csv")])
        if path:
            self._run_export(export_csv, Path(path))

    def _export_excel(self) -> None:
        path = filedialog.asksaveasfilename(parent=self.root, title="Экспорт Excel", defaultextension=".xlsx", filetypes=[("Excel", "*.xlsx")])
        if path:
            self._run_export(export_excel, Path(path))

    def _run_export(self, exporter: Any, path: Path) -> None:
        try:
            count = exporter(self.database.all(), path)
        except NoMeasurementsError:
            messagebox.showinfo("Экспорт", "Нет измерений для экспорта", parent=self.root)
        except (OSError, ValueError) as error:
            messagebox.showerror("Ошибка экспорта", str(error), parent=self.root)
        else:
            messagebox.showinfo("Экспорт", "Сохранено измерений: {}".format(count), parent=self.root)

    def close(self) -> None:
        self.closing = True
        self.manager.close()
        self.database.close()
        self.root.destroy()
