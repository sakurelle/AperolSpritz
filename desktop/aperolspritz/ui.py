import math
import queue
import uuid
from pathlib import Path
from tkinter import BooleanVar, StringVar, Tk, filedialog, messagebox, simpledialog, ttk
from typing import Any, Dict, List, Optional, Set, Tuple

from .api import ConnectionManager
from .commands import CONFIG_FIELDS, CommandManager, calculate_mm_per_step
from .database import MeasurementDatabase
from .exporter import NoMeasurementsError, export_csv, export_excel
from .models import Device, MeasurementEvent, result_status
from .settings import SettingsStore, normalize_address


STATE_NAMES = {
    "IDLE": "Ожидание", "MEASURE_FAST": "Быстрый проход", "MEASURE_RETRACT": "Отъезд",
    "MEASURE_FINE": "Точное измерение", "MEASURE_FINAL_RETRACT": "Финальный отъезд",
    "CALIBRATION_PREPARE": "Подготовка калибровки", "AUTO_CALIBRATION_PREPARE": "Подготовка автокалибровки",
    "CALIBRATE_FAST": "Калибровка: быстрый проход", "CALIBRATE_RETRACT": "Калибровка: отъезд",
    "CALIBRATE_FINE": "Калибровка: точное касание", "CALIBRATE_FINAL_RETRACT": "Финальный отъезд",
    "AUTO_WAIT_SENSOR_CLEAR": "Ожидание освобождения датчика", "AUTO_WAIT_NEEDLE": "Ожидание иглы",
    "AUTO_WAIT_REMOVE": "Ожидание снятия иглы", "AUTO_MOVE_TO_LOAD": "Возврат в загрузочную позицию",
    "MANUAL_LEFT": "Ручное перемещение", "SERVICE_MOVE": "Сервисное перемещение",
    "FINISHED": "Операция завершена", "STOPPED": "STOP", "ERROR": "Ошибка",
}
ERROR_NAMES = {
    "NONE": "Нет", "NEEDLE_NOT_FOUND": "Игла не обнаружена", "CONTACT_ALREADY_ACTIVE": "CONTACT уже активен",
    "CONTACT_RELEASE_TIMEOUT": "CONTACT не освободился вовремя", "MEASUREMENT_TIMEOUT": "Тайм-аут измерения",
    "CALIBRATION_TIMEOUT": "Тайм-аут калибровки", "MAX_STEPS_REACHED": "Достигнут лимит шагов",
    "NOT_CALIBRATED": "Устройство не откалибровано", "STOP_ACTIVE": "Активен STOP",
    "INVALID_CONFIG": "Некорректные настройки", "INTERNAL_ERROR": "Внутренняя ошибка",
}
RESULT_NAMES = {"OK": "НОРМА", "NEAR_LIMIT": "БЛИЗКО К ГРАНИЦЕ", "OUT_OF_TOLERANCE": "ВНЕ ДОПУСКА"}
IDLE_STATES = {"IDLE", "FINISHED", "ERROR", "STOPPED"}
CONFIG_NUMERIC_FIELDS = (
    "nominal_length_mm", "tolerance_mm", "calibration_length_mm",
    "auto_calibration_length_mm", "retract_mm", "auto_calibration_interval",
)
OPERATION_LABELS = {
    "measure": "измерение", "calibrate": "калибровку", "auto_start": "включение AUTO",
    "auto_stop": "выключение AUTO", "stop": "STOP", "reset_stop": "сброс STOP",
    "reset_error": "сброс ошибки", "get_config": "загрузку настроек", "save_config": "сохранение настроек",
    "service_move": "сервисное перемещение", "set_mm_per_step": "изменение коэффициента",
    "factory_reset": "сброс настроек устройства",
}


class MonitorWindow:
    def __init__(self, root: Tk, settings: SettingsStore, database: MeasurementDatabase) -> None:
        self.root, self.settings, self.database = root, settings, database
        self.devices = settings.load()
        self.events: "queue.Queue[Tuple[Any, ...]]" = queue.Queue()
        self.manager = ConnectionManager(settings.client_id, self.events)
        self.commands = CommandManager(self.events)
        self.statuses: Dict[str, Dict[str, Any]] = {}
        self.online: Dict[str, bool] = {}
        self.pending: Dict[str, Set[str]] = {}
        self.current_device_id: Optional[str] = self.devices[0].local_id if self.devices else None
        self.config_device_id: Optional[str] = None
        self.config_dirty = self.config_loading = self.closing = False
        self.last_service_command: Dict[str, float] = {}
        self.recommended_mm_per_step: Dict[str, float] = {}

        self.device_var, self.name_var, self.ip_var = StringVar(), StringVar(), StringVar()
        self.connection_var = StringVar(value="● Нет выбранного устройства")
        self.footer_var = StringVar(value="Состояние: нет выбранного устройства")
        self.message_var = StringVar()
        self.length_var, self.result_var = StringVar(value="—"), StringVar(value="Нет измерения")
        self.deviation_var = StringVar(value="Отклонение: —")
        self.nominal_var, self.tolerance_var = StringVar(value="Номинал: —"), StringVar(value="Допуск: —")
        self.auto_button_var = StringVar(value="Включить авто")
        self.status_vars: Dict[str, StringVar] = {}
        self.config_vars = {name: StringVar() for name in CONFIG_NUMERIC_FIELDS}
        self.config_dir_var = BooleanVar(value=False)
        self.service_distance_var, self.service_actual_var = StringVar(value="-100.000"), StringVar()
        service_vars = [StringVar(value="—") for _ in range(9)]
        (self.service_current_var, self.service_steps_var, self.service_last_var, self.service_expected_var,
         self.service_actual_display_var, self.service_error_var, self.service_error_percent_var,
         self.service_recommended_var, self.service_recommended_steps_var) = service_vars

        self._build()
        for variable in list(self.config_vars.values()) + [self.config_dir_var]:
            variable.trace_add("write", self._config_changed)
        self._refresh_device_choices()
        self._load_history()
        self.manager.update_devices(self.devices)
        if self.current_device_id:
            self._request_config()
        self.root.after(100, self._drain_events)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _build(self) -> None:
        self.root.title("AperolSpritz")
        self.root.geometry("1000x750")
        self.root.minsize(850, 650)
        style = ttk.Style(self.root)
        style.configure("Result.TLabel", font=("Segoe UI", 34, "bold"))
        style.configure("ResultState.TLabel", font=("Segoe UI", 15, "bold"))
        for name, color in (("Online.TLabel", "#18864b"), ("Offline.TLabel", "#a13a42"),
                            ("Ok.ResultState.TLabel", "#18864b"), ("Near.ResultState.TLabel", "#a86f00"),
                            ("Bad.ResultState.TLabel", "#bd2d35"), ("Emergency.TButton", "#9d1018")):
            style.configure(name, foreground=color)
        outer = ttk.Frame(self.root, padding=12)
        outer.pack(fill="both", expand=True)
        selector = ttk.LabelFrame(outer, text="Устройство", padding=8)
        selector.pack(fill="x")
        selector.columnconfigure(1, weight=1)
        ttk.Label(selector, text="Устройство:").grid(row=0, column=0, sticky="w", padx=(0, 8))
        self.device_combo = ttk.Combobox(selector, textvariable=self.device_var, state="readonly")
        self.device_combo.grid(row=0, column=1, sticky="ew")
        self.device_combo.bind("<<ComboboxSelected>>", self._select_combo)
        self.connection_label = ttk.Label(selector, textvariable=self.connection_var, style="Offline.TLabel")
        self.connection_label.grid(row=0, column=2, sticky="e", padx=(12, 0))
        ttk.Label(selector, text="Имя:").grid(row=1, column=0, sticky="w", pady=(7, 0))
        ttk.Entry(selector, textvariable=self.name_var).grid(row=1, column=1, sticky="ew", pady=(7, 0))
        ttk.Label(selector, text="IP:").grid(row=2, column=0, sticky="w", pady=(5, 0))
        ttk.Entry(selector, textvariable=self.ip_var).grid(row=2, column=1, sticky="ew", pady=(5, 0))
        actions = ttk.Frame(selector)
        actions.grid(row=1, column=2, rowspan=2, padx=(12, 0))
        for text, command in (("Добавить", self._add_device), ("Сохранить", self._save_device), ("Удалить", self._delete_device)):
            ttk.Button(actions, text=text, command=command).pack(side="left", padx=2)
        self.notebook = ttk.Notebook(outer)
        self.notebook.pack(fill="both", expand=True, pady=(10, 6))
        self.work_tab, self.settings_tab = ttk.Frame(self.notebook, padding=10), ttk.Frame(self.notebook, padding=10)
        self.service_tab, self.history_tab = ttk.Frame(self.notebook, padding=10), ttk.Frame(self.notebook, padding=10)
        for tab, text in ((self.work_tab, "Работа"), (self.settings_tab, "Настройки"),
                          (self.service_tab, "Сервис"), (self.history_tab, "История")):
            self.notebook.add(tab, text=text)
        self.notebook.bind("<<NotebookTabChanged>>", self._tab_changed)
        self._build_work_tab(); self._build_settings_tab(); self._build_service_tab(); self._build_history_tab()
        ttk.Label(outer, textvariable=self.message_var, foreground="#9d1018", wraplength=940).pack(fill="x")
        ttk.Separator(outer).pack(fill="x", pady=(5, 3))
        ttk.Label(outer, textvariable=self.footer_var).pack(fill="x")

    def _build_work_tab(self) -> None:
        self.work_tab.columnconfigure(0, weight=3); self.work_tab.columnconfigure(1, weight=2); self.work_tab.rowconfigure(1, weight=1)
        result = ttk.LabelFrame(self.work_tab, text="Последнее измерение", padding=14)
        result.grid(row=0, column=0, sticky="nsew", padx=(0, 6), pady=(0, 8))
        ttk.Label(result, textvariable=self.length_var, style="Result.TLabel", anchor="center").pack(fill="x", pady=(5, 8))
        for variable in (self.deviation_var, self.nominal_var, self.tolerance_var):
            ttk.Label(result, textvariable=variable, anchor="center").pack(fill="x")
        self.result_label = ttk.Label(result, textvariable=self.result_var, style="ResultState.TLabel", anchor="center")
        self.result_label.pack(fill="x", pady=(10, 0))
        controls = ttk.LabelFrame(self.work_tab, text="Управление", padding=12)
        controls.grid(row=0, column=1, sticky="nsew", padx=(6, 0), pady=(0, 8))
        self.measure_button = ttk.Button(controls, text="Измерить", command=lambda: self._send("measure"))
        self.calibrate_button = ttk.Button(controls, text="Калибровать", command=lambda: self._send("calibrate"))
        self.auto_button = ttk.Button(controls, textvariable=self.auto_button_var, command=self._toggle_auto)
        for button in (self.measure_button, self.calibrate_button, self.auto_button): button.pack(fill="x", pady=3)
        status = ttk.LabelFrame(self.work_tab, text="Состояние системы", padding=10)
        status.grid(row=1, column=0, sticky="nsew", padx=(0, 6)); status.columnconfigure(1, weight=1)
        fields = (("needle", "Игла"), ("contact", "CONTACT"), ("stop", "STOP"), ("error", "Ошибка"),
                  ("wifi", "Wi-Fi"), ("ip", "IP"), ("logger", "Приложение"), ("fsm", "FSM"), ("auto", "AUTO"),
                  ("phase", "Текущая фаза"), ("auto_phase", "Автофаза"),
                  ("calibration", "Калибровка"), ("source", "Источник калибровки"),
                  ("reference", "Опорная длина"), ("since", "Измерений после калибровки"),
                  ("interval", "Интервал автокалибровки"), ("remaining", "До следующей автокалибровки"))
        for row, (key, label) in enumerate(fields):
            self.status_vars[key] = StringVar(value="—")
            ttk.Label(status, text=label + ":").grid(row=row, column=0, sticky="w", padx=(0, 14), pady=1)
            ttk.Label(status, textvariable=self.status_vars[key]).grid(row=row, column=1, sticky="w", pady=1)
        emergency = ttk.LabelFrame(self.work_tab, text="Аварийное управление", padding=12)
        emergency.grid(row=1, column=1, sticky="new", padx=(6, 0))
        self.stop_button = ttk.Button(emergency, text="STOP", style="Emergency.TButton", command=lambda: self._send("stop"))
        self.reset_stop_button = ttk.Button(emergency, text="Сброс STOP", command=lambda: self._send("reset_stop"))
        self.reset_error_button = ttk.Button(emergency, text="Сброс ошибки", command=lambda: self._send("reset_error"))
        for button in (self.stop_button, self.reset_stop_button, self.reset_error_button): button.pack(fill="x", pady=4)
        ttk.Label(emergency, text="Программный STOP не гарантирован при отсутствии связи.", wraplength=250).pack(fill="x", pady=(8, 0))

    def _build_settings_tab(self) -> None:
        box = ttk.LabelFrame(self.settings_tab, text="Настройки оператора", padding=14)
        box.pack(fill="x"); box.columnconfigure(1, weight=1)
        labels = (("nominal_length_mm", "Номинальная длина, мм"), ("tolerance_mm", "Допуск, мм"),
                  ("calibration_length_mm", "Длина ручного калибра, мм"),
                  ("auto_calibration_length_mm", "Длина автоматического калибра, мм"),
                  ("retract_mm", "Расстояние отъезда, мм"),
                  ("auto_calibration_interval", "Автокалибровка каждые N измерений"))
        self.config_entries: List[ttk.Entry] = []
        for row, (key, text) in enumerate(labels):
            ttk.Label(box, text=text + ":").grid(row=row, column=0, sticky="w", padx=(0, 12), pady=5)
            entry = ttk.Entry(box, textvariable=self.config_vars[key], width=24)
            entry.grid(row=row, column=1, sticky="w", pady=5); self.config_entries.append(entry)
        self.config_check = ttk.Checkbutton(box, text="Инвертировать направление DIR", variable=self.config_dir_var)
        self.config_check.grid(row=len(labels), column=0, columnspan=2, sticky="w", pady=7)
        actions = ttk.Frame(box); actions.grid(row=len(labels) + 1, column=0, columnspan=2, sticky="w", pady=(10, 0))
        self.config_refresh_button = ttk.Button(actions, text="Обновить настройки", command=self._request_config)
        self.config_save_button = ttk.Button(actions, text="Сохранить настройки", command=self._save_config)
        self.config_refresh_button.pack(side="left"); self.config_save_button.pack(side="left", padx=8)
        ttk.Label(self.settings_tab, text="Настройки относятся только к выбранному устройству и сохраняются исключительно по кнопке.").pack(anchor="w", pady=12)

    def _build_service_tab(self) -> None:
        move = ttk.LabelFrame(self.service_tab, text="Сервисное движение", padding=12)
        move.pack(fill="x"); move.columnconfigure(1, weight=1)
        for row, (label, variable) in enumerate((("Текущий коэффициент:", self.service_current_var),
                                                  ("Шагов на мм:", self.service_steps_var),
                                                  ("Последняя команда:", self.service_last_var))):
            ttk.Label(move, text=label).grid(row=row, column=0, sticky="w", pady=3)
            ttk.Label(move, textvariable=variable).grid(row=row, column=1, sticky="w", pady=3)
        ttk.Label(move, text="Перемещение, мм:").grid(row=3, column=0, sticky="w", pady=(10, 3))
        ttk.Entry(move, textvariable=self.service_distance_var, width=22).grid(row=3, column=1, sticky="w", pady=(10, 3))
        self.service_move_button = ttk.Button(move, text="Переместить", command=self._service_move)
        self.service_move_button.grid(row=3, column=2, padx=(8, 0), pady=(10, 3))
        ttk.Label(move, text="− влево, + вправо. Абсолютной HOME-позиции нет.").grid(row=4, column=0, columnspan=3, sticky="w", pady=(7, 0))
        correction = ttk.LabelFrame(self.service_tab, text="Коррекция коэффициента", padding=12)
        correction.pack(fill="x", pady=(10, 0)); correction.columnconfigure(1, weight=1)
        ttk.Label(correction, text="Фактическое перемещение, мм:").grid(row=0, column=0, sticky="w", pady=3)
        ttk.Entry(correction, textvariable=self.service_actual_var, width=22).grid(row=0, column=1, sticky="w", pady=3)
        rows = (("Заданное перемещение:", self.service_expected_var), ("Фактическое перемещение:", self.service_actual_display_var),
                ("Ошибка:", self.service_error_var), ("Ошибка, %:", self.service_error_percent_var),
                ("Рекомендуемый коэффициент:", self.service_recommended_var),
                ("Рекомендуемые шаги на мм:", self.service_recommended_steps_var))
        for row, (label, variable) in enumerate(rows, 1):
            ttk.Label(correction, text=label).grid(row=row, column=0, sticky="w", pady=2)
            ttk.Label(correction, textvariable=variable).grid(row=row, column=1, sticky="w", pady=2)
        actions = ttk.Frame(correction); actions.grid(row=7, column=0, columnspan=2, sticky="w", pady=(9, 0))
        self.calculate_button = ttk.Button(actions, text="Рассчитать", command=self._calculate_service)
        self.apply_coefficient_button = ttk.Button(actions, text="Применить коэффициент", command=self._apply_coefficient)
        self.calculate_button.pack(side="left"); self.apply_coefficient_button.pack(side="left", padx=8)
        maintenance = ttk.LabelFrame(self.service_tab, text="Обслуживание устройства", padding=12)
        maintenance.pack(fill="x", pady=(10, 0))
        self.factory_reset_button = ttk.Button(maintenance, text="Сбросить настройки устройства", command=self._factory_reset)
        self.factory_reset_button.pack(anchor="w")
        ttk.Label(maintenance, text="Локальная SQLite и история Desktop не удаляются.").pack(anchor="w", pady=(6, 0))

    def _build_history_tab(self) -> None:
        history = ttk.LabelFrame(self.history_tab, text="Последние измерения", padding=8)
        history.pack(fill="both", expand=True)
        columns = ("time", "device", "mode", "length", "deviation", "result", "calibration")
        self.tree = ttk.Treeview(history, columns=columns, show="headings", height=16)
        for column, heading, width in zip(columns, ("Дата / время", "Устройство", "Режим", "Длина", "Отклонение", "Результат", "Калибровка"),
                                          (145, 170, 75, 90, 100, 150, 100)):
            self.tree.heading(column, text=heading); self.tree.column(column, width=width, anchor="center")
        for tag, color in (("ok", "#18864b"), ("near", "#a86f00"), ("bad", "#bd2d35")): self.tree.tag_configure(tag, foreground=color)
        scrollbar = ttk.Scrollbar(history, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set); self.tree.pack(side="left", fill="both", expand=True); scrollbar.pack(side="right", fill="y")
        actions = ttk.Frame(self.history_tab); actions.pack(fill="x", pady=(8, 0))
        ttk.Button(actions, text="Экспорт Excel", command=self._export_excel).pack(side="left")
        ttk.Button(actions, text="Экспорт CSV", command=self._export_csv).pack(side="left", padx=8)
        ttk.Label(actions, text="Экспортируется вся SQLite, а не только видимые строки.").pack(side="right")

    def _device(self, local_id: Optional[str] = None) -> Optional[Device]:
        wanted = local_id or self.current_device_id
        return next((item for item in self.devices if item.local_id == wanted), None)

    def _pending(self, local_id: Optional[str], operation: str) -> bool:
        return bool(local_id and operation in self.pending.get(local_id, set()))

    def _refresh_device_choices(self) -> None:
        self.device_combo["values"] = ["{} — {}".format(d.name, d.ip) for d in self.devices]
        current = self._device()
        if current:
            self.device_combo.current(self.devices.index(current)); self.name_var.set(current.name); self.ip_var.set(current.ip)
            status = self.statuses.get(current.local_id)
            self._show_status(status) if status and self.online.get(current.local_id) else self._show_offline("Нет связи")
        else:
            self.device_var.set(""); self.name_var.set(""); self.ip_var.set(""); self._show_offline("Нет выбранного устройства")

    def _confirm_config_transition(self) -> bool:
        if not self.config_dirty: return True
        answer = messagebox.askyesnocancel("Несохранённые настройки", "Настройки изменены. Сохранить их перед переходом?", parent=self.root)
        if answer is None: return False
        if answer: self._save_config(); return False
        self.config_dirty = False
        return True

    def _select_combo(self, _event: Any = None) -> None:
        index = self.device_combo.current()
        if index < 0 or index >= len(self.devices): return
        selected, previous = self.devices[index], self.current_device_id
        if selected.local_id == previous: return
        if not self._confirm_config_transition():
            self.current_device_id = previous; self._refresh_device_choices(); return
        self.current_device_id, self.config_device_id = selected.local_id, None
        self._clear_config(); self._refresh_device_choices(); self._request_config()

    def _add_device(self) -> None:
        if not self._confirm_config_transition(): return
        name = simpledialog.askstring("Новое устройство", "Понятное имя:", parent=self.root)
        if not name: return
        address = simpledialog.askstring("Новое устройство", "IP-адрес:", parent=self.root)
        if not address: return
        try: normalized = normalize_address(address)
        except ValueError as error: messagebox.showerror("Некорректный адрес", str(error), parent=self.root); return
        device = Device(str(uuid.uuid4()), name.strip(), normalized)
        self.devices.append(device); self.current_device_id, self.config_device_id = device.local_id, None
        self._persist_devices(); self._request_config()

    def _save_device(self) -> None:
        device = self._device()
        if not device: return
        name = self.name_var.get().strip()
        if not name: messagebox.showerror("Некорректное имя", "Введите имя устройства", parent=self.root); return
        try: address = normalize_address(self.ip_var.get())
        except ValueError as error: messagebox.showerror("Некорректный адрес", str(error), parent=self.root); return
        changed = device.ip != address
        device.name, device.ip = name, address
        if changed:
            self.statuses.pop(device.local_id, None); self.online[device.local_id] = False; self.config_device_id = None; self._clear_config()
        self._persist_devices()
        if changed: self._request_config()

    def _delete_device(self) -> None:
        device = self._device()
        if not device or not self._confirm_config_transition(): return
        if not messagebox.askyesno("Удаление", "Удалить устройство «{}»? История останется в SQLite.".format(device.name), parent=self.root): return
        self.devices = [d for d in self.devices if d.local_id != device.local_id]
        for mapping in (self.statuses, self.online, self.pending): mapping.pop(device.local_id, None)
        self.current_device_id = self.devices[0].local_id if self.devices else None
        self.config_device_id = None; self._clear_config(); self._persist_devices()
        if self.current_device_id: self._request_config()

    def _persist_devices(self) -> None:
        try: self.settings.save(self.devices)
        except OSError as error: messagebox.showerror("Ошибка сохранения", str(error), parent=self.root); return
        self._refresh_device_choices(); self.manager.update_devices(self.devices)

    def _tab_changed(self, _event: Any = None) -> None:
        if self.notebook.select() == str(self.settings_tab) and self.current_device_id != self.config_device_id: self._request_config()

    def _show_offline(self, text: str) -> None:
        self.connection_var.set("● {}".format(text)); self.connection_label.configure(style="Offline.TLabel")
        self.footer_var.set("Состояние: нет связи"); self.length_var.set("—"); self.deviation_var.set("Отклонение: —")
        self.nominal_var.set("Номинал: —"); self.tolerance_var.set("Допуск: —"); self.result_var.set("Нет актуальных данных")
        self.result_label.configure(style="ResultState.TLabel")
        for variable in self.status_vars.values(): variable.set("Нет связи")
        self._update_buttons()

    def _show_status(self, status: Dict[str, Any]) -> None:
        device = self._device()
        if not device: return
        self.connection_var.set("● Подключено — {}".format(device.ip)); self.connection_label.configure(style="Online.TLabel")
        nominal, tolerance = float(status.get("nominal_length_mm", 0)), float(status.get("tolerance_mm", 0))
        self.nominal_var.set("Номинал: {:.3f} мм".format(nominal)); self.tolerance_var.set("Допуск: ±{:.3f} мм".format(tolerance))
        if status.get("result") not in (None, "", "NONE"):
            length, deviation = float(status.get("last_length_mm", 0)), float(status.get("deviation_mm", 0))
            classification = result_status(deviation, tolerance)
            self.length_var.set("{:.3f} мм".format(length)); self.deviation_var.set("Отклонение: {:+.3f} мм".format(deviation))
            self.result_var.set(RESULT_NAMES[classification]); self.result_label.configure(style={"OK": "Ok.ResultState.TLabel", "NEAR_LIMIT": "Near.ResultState.TLabel", "OUT_OF_TOLERANCE": "Bad.ResultState.TLabel"}[classification])
        else:
            self.length_var.set("—"); self.deviation_var.set("Отклонение: —"); self.result_var.set("Нет измерения"); self.result_label.configure(style="ResultState.TLabel")
        state, error = str(status.get("state", "")), str(status.get("error", "NONE"))
        stop_active = bool(status.get("stop_active")) or state == "STOPPED"
        source = str(status.get("calibration_source", "NONE"))
        values = {
            "needle": "Обнаружена" if status.get("needle_present") else "Отсутствует",
            "contact": "Активен" if status.get("contact") else "Свободен", "stop": "АКТИВЕН" if stop_active else "Нормально",
            "error": ERROR_NAMES.get(error, error), "wifi": "{}: {}".format(status.get("wifi_mode", "—"), "подключён" if status.get("wifi_connected") else "отключён"),
            "ip": str(status.get("ip_address") or device.ip),
            "logger": ("Подключено — {}".format(status.get("logger_client_name")) if status.get("logger_client_name") else "Подключено") if status.get("logger_connected") else "Не подключено",
            "fsm": state or "—",
            "auto": "Выключение…" if status.get("auto_stop_requested") else ("ВКЛ" if status.get("auto_mode_enabled") else "ВЫКЛ"),
            "phase": STATE_NAMES.get(state, state or "—"), "auto_phase": str(status.get("auto_phase") or "—"),
            "calibration": "Действительна" if status.get("calibration_valid") else "Недействительна",
            "source": {"AUTO": "Автоматическая", "MANUAL": "Ручная", "NONE": "Нет"}.get(source, source),
            "reference": "{:.3f} мм".format(float(status.get("calibration_reference_length_mm", 0))) if status.get("calibration_valid") else "—",
            "since": str(status.get("measurements_since_calibration", 0)), "interval": "{} измерений".format(status.get("auto_calibration_interval", 1)),
            "remaining": str(status.get("auto_calibration_remaining", 0)),
        }
        for key, value in values.items(): self.status_vars[key].set(value)
        self.footer_var.set("Состояние: {}".format(values["phase"]))
        coefficient = float(status.get("mm_per_step", 0))
        self.service_current_var.set("{:.9f} мм/шаг".format(coefficient) if coefficient > 0 else "—")
        self.service_steps_var.set("{:.3f} шаг/мм".format(1 / coefficient) if coefficient > 0 else "—")
        reported = float(status.get("service_move_requested_mm", 0))
        if reported: self.last_service_command[device.local_id] = reported
        last = self.last_service_command.get(device.local_id, 0)
        self.service_last_var.set("{:+.3f} мм".format(last) if last else "—")
        self._update_buttons()

    def _update_buttons(self) -> None:
        local_id, status = self.current_device_id, self.statuses.get(self.current_device_id or "", {})
        online, state, error = bool(local_id and self.online.get(local_id)), str(status.get("state", "")), str(status.get("error", "NONE"))
        auto = bool(status.get("auto_mode_enabled")); stop_active = bool(status.get("stop_active")) or state == "STOPPED"; busy = state not in IDLE_STATES
        normal = online and not auto and not stop_active and error == "NONE" and not busy
        self.measure_button.configure(state="normal" if normal and not self._pending(local_id, "measure") else "disabled")
        self.calibrate_button.configure(state="normal" if normal and not self._pending(local_id, "calibrate") else "disabled")
        self.auto_button_var.set("Выключение AUTO…" if status.get("auto_stop_requested") else ("Выключить авто" if auto else "Включить авто"))
        auto_op = "auto_stop" if auto else "auto_start"; auto_allowed = online and error == "NONE" and not stop_active and (auto or not busy)
        self.auto_button.configure(state="normal" if auto_allowed and not self._pending(local_id, auto_op) else "disabled")
        self.stop_button.configure(state="normal" if online and not self._pending(local_id, "stop") else "disabled")
        self.reset_stop_button.configure(state="normal" if online and state == "STOPPED" and not self._pending(local_id, "reset_stop") else "disabled")
        self.reset_error_button.configure(state="normal" if online and state == "ERROR" and not self._pending(local_id, "reset_error") else "disabled")
        config_allowed = online and state == "IDLE" and error == "NONE" and not auto
        self.config_refresh_button.configure(state="normal" if online and not self._pending(local_id, "get_config") else "disabled")
        self.config_save_button.configure(state="normal" if config_allowed and self.config_dirty and not self._pending(local_id, "save_config") else "disabled")
        for entry in self.config_entries: entry.configure(state="normal" if config_allowed else "disabled")
        self.config_check.configure(state="normal" if config_allowed else "disabled")
        self.service_move_button.configure(state="normal" if normal and not self._pending(local_id, "service_move") else "disabled")
        self.calculate_button.configure(state="normal" if online else "disabled")
        self.apply_coefficient_button.configure(state="normal" if normal and local_id in self.recommended_mm_per_step and not self._pending(local_id, "set_mm_per_step") else "disabled")
        self.factory_reset_button.configure(state="normal" if config_allowed and not self._pending(local_id, "factory_reset") else "disabled")

    def _toggle_auto(self) -> None:
        self._send("auto_stop" if self.statuses.get(self.current_device_id or "", {}).get("auto_mode_enabled") else "auto_start")

    def _send(self, operation: str, *args: Any) -> Optional[str]:
        device = self._device()
        if not device or not self.online.get(device.local_id): self._show_message("Нет связи с выбранным устройством."); return None
        if self._pending(device.local_id, operation): return None
        request_id = self.commands.submit(device, operation, *args)
        self.pending.setdefault(device.local_id, set()).add(operation)
        self._show_message("Отправлена команда: {}.".format(OPERATION_LABELS.get(operation, operation)), False); self._update_buttons()
        return request_id

    def _request_config(self) -> None:
        device = self._device()
        if not device: return
        if self.config_dirty and self.config_device_id == device.local_id:
            if not messagebox.askyesno("Обновить настройки", "Отменить несохранённые изменения?", parent=self.root): return
            self.config_dirty = False
        self._send("get_config")

    def _config_changed(self, *_args: Any) -> None:
        if not self.config_loading and self.config_device_id == self.current_device_id: self.config_dirty = True; self._update_buttons()

    def _clear_config(self) -> None:
        self.config_loading = True
        try:
            for variable in self.config_vars.values(): variable.set("")
            self.config_dir_var.set(False)
        finally: self.config_loading = False
        self.config_dirty = False

    def _put_config(self, local_id: str, config: Dict[str, Any]) -> None:
        if local_id != self.current_device_id: return
        self.config_loading = True
        try:
            for key in CONFIG_NUMERIC_FIELDS:
                value = config.get(key, ""); self.config_vars[key].set(str(int(value) if key == "auto_calibration_interval" and value != "" else value))
            self.config_dir_var.set(bool(config.get("measure_dir_inverted")))
        finally: self.config_loading = False
        self.config_device_id, self.config_dirty = local_id, False; self._update_buttons()

    def _validated_config(self) -> Dict[str, Any]:
        values: Dict[str, Any] = {}
        for key in CONFIG_NUMERIC_FIELDS:
            try: number = float(self.config_vars[key].get().strip().replace(",", "."))
            except ValueError as error: raise ValueError("Поле «{}» содержит некорректное число".format(key)) from error
            if not math.isfinite(number): raise ValueError("Все числовые значения должны быть конечными")
            values[key] = int(number) if key == "auto_calibration_interval" and number.is_integer() else number
        if not 0 <= values["nominal_length_mm"] <= 1000: raise ValueError("Номинальная длина должна быть от 0 до 1000 мм")
        if not 0 <= values["tolerance_mm"] <= 100: raise ValueError("Допуск должен быть от 0 до 100 мм")
        if any(not 0 <= values[key] <= 1000 for key in ("calibration_length_mm", "auto_calibration_length_mm")): raise ValueError("Длина калибра должна быть от 0 до 1000 мм")
        if not 0.1 <= values["retract_mm"] <= 20: raise ValueError("Расстояние отъезда должно быть от 0,1 до 20 мм")
        interval = values["auto_calibration_interval"]
        if not isinstance(interval, int) or not 1 <= interval <= 1000: raise ValueError("Интервал автокалибровки должен быть целым числом от 1 до 1000")
        values["measure_dir_inverted"] = bool(self.config_dir_var.get())
        if set(values) != set(CONFIG_FIELDS): raise ValueError("Не удалось сформировать полный набор настроек")
        return values

    def _save_config(self) -> None:
        device = self._device()
        if not device or self.config_device_id != device.local_id: self._show_message("Сначала загрузите настройки выбранного устройства."); return
        try: config = self._validated_config()
        except ValueError as error: messagebox.showerror("Некорректные настройки", str(error), parent=self.root); return
        self._send("save_config", config)

    def _service_move(self) -> None:
        try: distance = float(self.service_distance_var.get().strip().replace(",", "."))
        except ValueError: messagebox.showerror("Сервисное движение", "Введите числовое перемещение.", parent=self.root); return
        if not math.isfinite(distance) or distance == 0 or abs(distance) > 150:
            messagebox.showerror("Сервисное движение", "Введите ненулевое перемещение не более 150 мм.", parent=self.root); return
        if abs(distance) >= 50:
            direction = "вправо" if distance > 0 else "влево"
            if not messagebox.askyesno("Большое сервисное перемещение", "Переместить каретку {} на {:.3f} мм?\n\nУбедитесь, что ход свободен.".format(direction, abs(distance)), parent=self.root): return
        device = self._device()
        if device: self.last_service_command[device.local_id] = distance; self.recommended_mm_per_step.pop(device.local_id, None); self._clear_service_calculation()
        self._send("service_move", distance)

    def _clear_service_calculation(self) -> None:
        for variable in (self.service_expected_var, self.service_actual_display_var, self.service_error_var,
                         self.service_error_percent_var, self.service_recommended_var, self.service_recommended_steps_var): variable.set("—")
        self._update_buttons()

    def _calculate_service(self) -> None:
        local_id, status = self.current_device_id or "", self.statuses.get(self.current_device_id or "", {})
        commanded = self.last_service_command.get(local_id, float(status.get("service_move_requested_mm", 0)))
        try:
            actual = float(self.service_actual_var.get().strip().replace(",", ".")); old = float(status.get("mm_per_step", 0))
            recommended = calculate_mm_per_step(old, actual, commanded)
        except (ValueError, TypeError): messagebox.showerror("Расчёт коэффициента", "Выполните перемещение и введите положительное фактическое расстояние.", parent=self.root); return
        expected, error = abs(commanded), actual - abs(commanded)
        self.recommended_mm_per_step[local_id] = recommended
        self.service_expected_var.set("{:.3f} мм".format(expected)); self.service_actual_display_var.set("{:.3f} мм".format(actual))
        self.service_error_var.set("{:+.3f} мм".format(error)); self.service_error_percent_var.set("{:+.3f} %".format(error / expected * 100))
        self.service_recommended_var.set("{:.9f} мм/шаг".format(recommended)); self.service_recommended_steps_var.set("{:.3f} шаг/мм".format(1 / recommended)); self._update_buttons()

    def _apply_coefficient(self) -> None:
        local_id = self.current_device_id or ""; recommended = self.recommended_mm_per_step.get(local_id)
        current = float(self.statuses.get(local_id, {}).get("mm_per_step", 0))
        if not recommended: return
        if messagebox.askyesno("Применить коэффициент", "Изменить коэффициент с\n{:.9f}\nна\n{:.9f} мм/шаг?\n\nПосле изменения нужна новая калибровка.".format(current, recommended), parent=self.root): self._send("set_mm_per_step", recommended)

    def _factory_reset(self) -> None:
        device = self._device()
        if not device: return
        first = messagebox.askyesno("Сброс настроек устройства", "Сбросить конфигурацию и статистику калибровки ESP32 «{}»?\n\nSQLite Desktop останется без изменений.".format(device.name), parent=self.root)
        if first and messagebox.askyesno("Подтвердите сброс", "Это второе подтверждение. Продолжить?", parent=self.root): self._send("factory_reset")

    def _command_result(self, event: Tuple[Any, ...]) -> None:
        _, local_id, operation, _request_id, success, payload, detail, status_code, ambiguous, target_ip = event
        self.pending.setdefault(local_id, set()).discard(operation)
        if success:
            if operation == "get_config": self._put_config(local_id, payload or {})
            elif operation == "save_config" and local_id == self.current_device_id:
                self.config_dirty = False; self.config_device_id = local_id; self._show_message("Настройки сохранены.", False)
            elif operation == "service_move": self._show_message("Сервисное перемещение принято устройством.", False)
            elif operation == "set_mm_per_step": self.recommended_mm_per_step.pop(local_id, None); self._show_message("Коэффициент обновлён. Необходимо выполнить новую калибровку.", False)
            elif operation == "factory_reset":
                self._show_message("Настройки устройства сброшены. Загружаю актуальную конфигурацию.", False)
                if local_id == self.current_device_id: self._request_config()
            else: self._show_message("Команда «{}» принята устройством {}.".format(OPERATION_LABELS.get(operation, operation), target_ip), False)
        elif local_id == self.current_device_id:
            code = "HTTP {}. ".format(status_code) if status_code else ""
            suffix = " Команда могла быть принята; автоматический повтор не выполнялся. Состояние обновится из /api/status." if ambiguous else ""
            self._show_message("Не удалось выполнить {}. Причина: {}{}{}".format(OPERATION_LABELS.get(operation, operation), code, self._translate_api_error(detail), suffix))
        self._update_buttons()

    @staticmethod
    def _translate_api_error(detail: str) -> str:
        return {"Controller busy": "Устройство занято.", "Command queue full": "Очередь команд заполнена.",
                "Settings can be changed only in IDLE": "Настройки можно менять только в состоянии ожидания.",
                "STOP active": "Активен STOP.", "Not calibrated": "Устройство не откалибровано.",
                "Invalid config": "Некорректные настройки.", "Controller unavailable": "Контроллер недоступен."}.get(detail, detail or "Неизвестная ошибка")

    def _show_message(self, text: str, error: bool = True) -> None:
        self.message_var.set(text)

    def _drain_events(self) -> None:
        if self.closing: return
        try:
            while True:
                event = self.events.get_nowait(); kind, local_id = event[0], event[1]
                if kind == "command_result": self._command_result(event); continue
                device = self._device(local_id)
                if not device: continue
                if kind == "status":
                    self.statuses[local_id], self.online[local_id] = event[2], True
                    if local_id == self.current_device_id:
                        self._show_status(event[2])
                        if self.config_device_id != local_id and not self._pending(local_id, "get_config"):
                            self._request_config()
                elif kind == "offline":
                    self.online[local_id] = False
                    if local_id == self.current_device_id: self._show_offline("Нет связи")
                elif kind == "history_gap" and local_id == self.current_device_id: self._show_message("Часть измерений могла быть пропущена: буфер истории ESP32 переполнен.")
                elif kind == "measurement" and self.database.insert(device, event[2]): self._insert_measurement(device, event[2])
        except queue.Empty: pass
        self.root.after(100, self._drain_events)

    def _insert_measurement(self, device: Device, event: MeasurementEvent) -> None:
        classification = result_status(event.deviation_mm, event.tolerance_mm); tag = {"OK": "ok", "NEAR_LIMIT": "near", "OUT_OF_TOLERANCE": "bad"}[classification]
        from datetime import datetime
        values = (datetime.now().astimezone().strftime("%d.%m.%Y %H:%M:%S"), device.name, event.mode, "{:.3f}".format(event.length_mm), "{:+.3f}".format(event.deviation_mm), RESULT_NAMES[classification], event.calibration_source)
        self.tree.insert("", 0, values=values, tags=(tag,)); children = self.tree.get_children()
        if len(children) > 200: self.tree.delete(children[-1])

    def _load_history(self) -> None:
        for row in reversed(self.database.latest(200)):
            classification = result_status(row["deviation_mm"], row["tolerance_mm"]); tag = {"OK": "ok", "NEAR_LIMIT": "near", "OUT_OF_TOLERANCE": "bad"}[classification]
            values = (str(row["timestamp_local"]).replace("T", " ")[:19], row["device_name"], row["mode"], "{:.3f}".format(row["length_mm"]), "{:+.3f}".format(row["deviation_mm"]), RESULT_NAMES[classification], row["calibration_source"])
            self.tree.insert("", 0, values=values, tags=(tag,))

    def _export_csv(self) -> None:
        path = filedialog.asksaveasfilename(parent=self.root, title="Экспорт CSV", defaultextension=".csv", filetypes=[("CSV", "*.csv")])
        if path: self._run_export(export_csv, Path(path))

    def _export_excel(self) -> None:
        path = filedialog.asksaveasfilename(parent=self.root, title="Экспорт Excel", defaultextension=".xlsx", filetypes=[("Excel", "*.xlsx")])
        if path: self._run_export(export_excel, Path(path))

    def _run_export(self, exporter: Any, path: Path) -> None:
        try: count = exporter(self.database.all(), path)
        except NoMeasurementsError: messagebox.showinfo("Экспорт", "Нет измерений для экспорта", parent=self.root)
        except (OSError, ValueError) as error: messagebox.showerror("Ошибка экспорта", str(error), parent=self.root)
        else: messagebox.showinfo("Экспорт", "Сохранено измерений: {}".format(count), parent=self.root)

    def close(self) -> None:
        if self.config_dirty and not self._confirm_config_transition(): return
        self.closing = True; self.manager.close(); self.commands.close(); self.database.close(); self.root.destroy()
