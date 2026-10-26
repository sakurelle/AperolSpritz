# AperolSpritz

AperolSpritz — система автоматизированного измерения длины игл на базе ESP32-C3.

Устройство работает в ручном и автоматическом режимах. Настольное приложение
собирает измерения с нескольких установок, сохраняет историю локально и позволяет
экспортировать результаты в Excel или CSV.

## Компоненты

- **Firmware** — прошивка ESP32-C3: управление двигателем TMC2209, измерение,
  калибровка, физические органы управления, Web UI и REST API.
- **Desktop** — Windows-приложение для мониторинга нескольких измерителей,
  журналирования в SQLite и экспорта данных.
- **Mechanical** — механические модели устройства в форматах CAD и STEP.

## Скачать приложение

Готовая Windows-сборка доступна в разделе
[Releases](https://github.com/sakurelle/AperolSpritz/releases/latest).

Скачайте `AperolSpritz-Windows-x64.zip`, полностью распакуйте архив и запустите
`AperolSpritz\AperolSpritz.exe`. Установленный Python не требуется. Каталог
`_internal` должен оставаться рядом с EXE.

Подтверждённая целевая платформа готовой сборки — Windows 11 x64.

## Документация

- [Прошивка ESP32](firmware/README.md)
- [Настольное приложение](desktop/README.md)
- Механические модели: [`mechanical/`](mechanical/)

## Структура репозитория

```text
AperolSpritz/
├── firmware/            # ESP32-C3 / ESP-IDF / PlatformIO
├── desktop/             # Tkinter-приложение и сценарии сборки
├── mechanical/          # CAD- и STEP-модели
└── .github/workflows/   # автоматическая Windows-сборка и релиз
```

## Разработка

Инструкции по сборке, конфигурации Wi-Fi, GPIO и REST API находятся в
[`firmware/README.md`](firmware/README.md).

Запуск из исходников, локальная Windows/Linux-сборка и выпуск версий описаны в
[`desktop/README.md`](desktop/README.md).

Готовые бинарные файлы публикуются только в GitHub Releases и не хранятся в
истории репозитория.
