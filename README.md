# NeedleMeter — ESP32-C3 измеритель длины иглы

Прошивка использует **ESP-IDF** через PlatformIO (framework = espidf). Импульсы TMC2209 формирует GPTimer; управление двигателем — только STEP, DIR, EN. Web UI и REST API передают команды конечному автомату FreeRTOS и не двигают мотор непосредственно.

## Актуальная распиновка

| GPIO | Сигнал | Логика |
|---:|---|---|
| 0 | CALIBRATE | LOW — начать калибровку |
| 1 | MEASURE | LOW — начать измерение |
| 2 | STOP | LOW — аварийная остановка |
| 3 | CONTACT | LOW — измерительный контакт замкнут на GND |
| 4 | NEEDLE | LOW — игла обнаружена, HIGH — игла отсутствует |
| 5 | DIR | выход TMC2209 |
| 6 | STEP | выход TMC2209 |
| 7 | EN | LOW включает TMC2209 |
| 10 | LEFT / JOG | LOW — отъезд, пока удерживается |

STEP=LOW, DIR=LOW, EN=HIGH устанавливаются до NVS, Wi‑Fi и создания задач. Это не затрагивает GPIO3/GPIO4/GPIO10: входы CONTACT, NEEDLE и LEFT никогда временно не становятся выходами.

### Подключение NEEDLE

NEEDLE — независимый оптический датчик. Его подключение:

    VCC → согласно питанию конкретного модуля
    GND → общая GND
    OUT → GPIO4

GPIO4 — обычный цифровой вход без внутренней подтяжки: модуль сам формирует уровень. Фактически измеренная логика:

    NEEDLE OUT LOW  = игла обнаружена
    NEEDLE OUT HIGH = игла отсутствует

CONTACT GPIO3 — отдельный active-low вход с pullup: HIGH = свободен, LOW = измерительный контакт замкнут на GND. NEEDLE и CONTACT электрически независимы.

## Последовательность измерения и калибровки

Измерение и калибровка используют одинаковую двухпроходную последовательность:

    FAST_APPROACH → CONTACT → RETRACT
                  → FINE_APPROACH → CONTACT → FINAL_RETRACT → FINISHED

В итог берётся только координата CONTACT медленного (FINE) подхода. После каждого касания каретка уходит от зафиксированной точки CONTACT ровно на настраиваемое расстояние `retract_mm` (по умолчанию 2.0 мм), переведённое в шаги через `llround(retract_mm / mm_per_step)`. CONTACT должен разомкнуться к концу этого движения; иначе операция завершается ошибкой.

Обычное измерение запускается только при обнаруженной игле (GPIO4 LOW) и продолжает контролировать оптический датчик в `MEASURE_FAST`, `MEASURE_RETRACT`, `MEASURE_FINE` и `MEASURE_FINAL_RETRACT`. Если GPIO4 остаётся HIGH не менее 5 мс, двигатель штатно останавливается, операция отменяется с `NEEDLE_NOT_FOUND`, а результат не сохраняется. CONTACT не меняет эту проверку. Калибровка и LEFT/JOG не зависят от NEEDLE.

Стандартные настройки: coarse 3000 шаг/с, fine 500 шаг/с, retract 1000 шаг/с, JOG 300 шаг/с, acceleration 6000 шаг/с². GPTimer остаётся генератором STEP; частота плавно возрастает из задачи FSM без blocking loop. STOP ISR немедленно сбрасывает STEP и устанавливает EN=HIGH из любого состояния.

LEFT/JOG всегда означает движение от контакта. Активный CONTACT не останавливает JOG и не создаёт цикл start/stop; отжатие LEFT даёт в лог JOG STOP reason=BUTTON_RELEASE.

### Относительная координата

position_steps — накопленная знаковая программная координата, обновляемая на каждом реально сформированном фронте STEP. Она не обнуляется между FAST, RETRACT, FINE и FINAL_RETRACT.

Она **не является абсолютной координатой**: достоверна только пока мотор не пропускает шаги и каретка не перемещалась вручную или при выключенном питании. HOME-датчика в устройстве нет. Длина вычисляется по разнице точных координат калибра и измерения:

    L = calibration_length_mm + measurement_sign × (calibration_position − measurement_position) × mm_per_step

## Wi‑Fi

Если build-time credentials настроены, устройство сначала подключается к лабораторной сети в режиме STA, получает DHCP-адрес и выводит SSID/IP. Поддерживаются:

- WPA2-PSK;
- WPA2-Enterprise (PEAP, identity/username/password) через ESP-IDF esp_eap_client.

При отсутствии credentials, ошибке конфигурации или таймауте подключения запускается fallback AP:

    SSID: NeedleMeter
    IP:   192.168.4.1

После потери STA-сети включается reconnect; работа кнопок и FSM от Wi‑Fi не зависит. /api/status и Web UI показывают режим Wi‑Fi и текущий IP. Пароль никогда не попадает в REST API, Web UI или log.

### secrets.txt

Скопируйте [secrets.example.txt](secrets.example.txt) в secrets.txt и укажите реальные данные. secrets.txt игнорируется Git и при сборке CMake создаёт заголовок только в build directory.

    WIFI_AUTH=enterprise
    WIFI_SSID=LAB_SSID
    WIFI_IDENTITY=my_login
    WIFI_USERNAME=my_login
    WIFI_PASSWORD=my_password

Для WPA2-PSK:

    WIFI_AUTH=psk
    WIFI_SSID=LAB_SSID
    WIFI_PASSWORD=my_password

Если файла нет или значения не заполнены, сборка остаётся рабочей и используется fallback AP.

## Web UI и API

Откройте http://<IP>/. Страница показывает фазу FSM, текущую скорость, программную координату, NEEDLE, CONTACT, последнее точное измерение, Wi‑Fi/IP и статистику. Настраиваемые coarse/fine/retract/JOG скорости, ускорение, отъезд и timeout сохраняются в NVS через POST /api/config.

API:

- GET /api/status, GET /api/config
- POST /api/config, /api/measure, /api/calibrate, /api/reset-stop, /api/reset-error, /api/factory-reset

## Сборка

    pio run
    pio run -t upload
    pio device monitor

ESP32-C3 console работает через USB Serial/JTAG, поэтому GPIO20/GPIO21 не заняты UART console.
