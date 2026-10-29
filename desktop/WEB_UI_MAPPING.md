# Соответствие Web UI и AperolSpritz Desktop

Источник истины: `firmware/main/web/web_server.cpp` и контроллер измерения текущей
рабочей копии. Таблица проверена по фактическим маршрутам, JSON и правилам
доступности кнопок Web UI.

| Функция Web UI | REST / данные | Desktop | Проверено |
|---|---|---|---|
| Последнее измерение, отклонение, номинал, допуск | `GET /api/status` | Работа → Последнее измерение | Да |
| Классификация зелёный/жёлтый/красный | `last_length_mm`, `nominal_length_mm`, `tolerance_mm`; граница 80% | Работа → результат и цвет текста | Да |
| Измерить | `POST /api/measure`, без body, `{accepted:true}` | Работа → Измерить | Да |
| Калибровать | `POST /api/calibrate`, без body, `{accepted:true}` | Работа → Калибровать | Да |
| AUTO ON | `POST /api/auto/start` | Работа → Включить авто | Да |
| AUTO OFF / безопасное завершение | `POST /api/auto/stop`; `auto_stop_requested` | Работа → Выключить авто / Выключение AUTO… | Да |
| STOP | `POST /api/stop` | Работа → Аварийное управление → STOP | Да, отдельный приоритетный executor |
| Reset STOP | `POST /api/reset-stop` | Работа → Сброс STOP | Да |
| Reset Error | `POST /api/reset-error` | Работа → Сброс ошибки | Да |
| Игла / CONTACT / STOP / ошибка | `GET /api/status` | Работа → Состояние системы | Да |
| Wi-Fi / IP | `wifi_mode`, `wifi_connected`, `ip_address` | Работа → Состояние системы | Да |
| Подключение Desktop в Web UI | `POST /api/logger/heartbeat`; `logger_*` в status | Фоновый heartbeat; состояние «Приложение» | Да |
| FSM, текущая фаза | `state` | Работа → FSM и Текущая фаза | Да |
| AUTO и автофаза | `auto_mode_enabled`, `auto_phase` | Работа → AUTO и Автофаза | Да |
| Действительность и источник калибровки | `calibration_valid`, `calibration_source` | Работа → Калибровка / Источник | Да |
| Опорная длина | `calibration_reference_length_mm` | Работа → Опорная длина | Да |
| Счётчики автокалибровки | `measurements_since_calibration`, `auto_calibration_interval`, `auto_calibration_remaining` | Работа → Состояние системы | Да |
| Чтение настроек | `GET /api/config`, полный объект | Настройки → Обновить настройки | Да |
| Номинал | `nominal_length_mm` | Настройки | Да |
| Допуск | `tolerance_mm` | Настройки | Да |
| Длина ручного калибра | `calibration_length_mm` | Настройки | Да |
| Длина автоматического калибра | `auto_calibration_length_mm` | Настройки | Да |
| Отъезд | `retract_mm` | Настройки | Да |
| Интервал автокалибровки | `auto_calibration_interval` | Настройки | Да |
| Инверсия DIR | `measure_dir_inverted` | Настройки | Да |
| Сохранение настроек | `POST /api/config`, все 7 операторских полей; `mm_per_step` запрещён | Настройки → Сохранить настройки | Да |
| Текущий mm/step и steps/mm | `mm_per_step` в status/config | Сервис → Сервисное движение | Да |
| Сервисное перемещение | `POST /api/service-move`, `{distance_mm}` | Сервис → Переместить | Да |
| Расчёт коррекции | `old * actual / abs(commanded)` | Сервис → Рассчитать | Да |
| Применение mm/step | `POST /api/service/mm-per-step`, `{mm_per_step}` | Сервис → Применить коэффициент | Да |
| Factory reset | `POST /api/factory-reset`, без body | Сервис → Обслуживание устройства | Да, двойное подтверждение |
| История измерений ESP32 | `GET /api/measurements?after_seq=N` | Фоновый сбор всех устройств | Да |
| Обнаружение пропуска | `history_gap` | Предупреждение Desktop | Да |
| История Desktop | SQLite | История → таблица | Да |
| Экспорт Excel / CSV | локальная SQLite | История → кнопки экспорта | Да |

`GET /api/status` также содержит внутренние поля скорости, позиции и целевого числа
шагов. Web UI не выводит их как отдельные операторские элементы, поэтому Desktop
не добавляет новое управление на их основе; `service_move_requested_mm` используется
только для сервисного резюме. Физическое ручное перемещение кнопкой GPIO не имеет
REST-команды и потому отображается как FSM `MANUAL_LEFT`, но не дублируется кнопкой
Desktop.
