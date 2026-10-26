# AperolSpritz Desktop

Tkinter-приложение для наблюдения за несколькими измерителями AperolSpritz.
Приложение опрашивает устройства независимо, автоматически сохраняет новые
измерения в SQLite и экспортирует всю историю в Excel или CSV.

## Готовое Windows-приложение

1. Откройте [Releases](https://github.com/sakurelle/AperolSpritz/releases/latest).
2. Скачайте `AperolSpritz-Windows-x64.zip`.
3. Полностью распакуйте архив.
4. Запустите `AperolSpritz\AperolSpritz.exe`.
5. Добавьте IP-адрес одного или нескольких ESP32 и проверьте подключение.
6. Используйте кнопки **Экспорт Excel** и **Экспорт CSV** для выгрузки истории.

Это `onedir`-сборка: не удаляйте `_internal` и не переносите отдельно один EXE.
Установленный Python и интернет для запуска не требуются. Наличие ESP32 в сети
не обязательно для открытия интерфейса. Подтверждённая целевая платформа —
Windows 11 x64.

## Пользовательские данные

Данные находятся вне каталога установки:

```text
%APPDATA%\AperolSpritz\settings.json
%APPDATA%\AperolSpritz\measurements.db
```

Каталог создаётся автоматически и не требует прав администратора. Удаление или
обновление папки с EXE не удаляет историю. SQLite — основное хранилище; Excel и
CSV создаются только по команде экспорта. CSV использует UTF-8 с BOM и разделитель
`;`. Реальные пользовательские настройки и базы в релизный архив не включаются.

## Запуск из исходников

Требуется Python 3.9 или новее с `tkinter`.

```powershell
cd desktop
python -m pip install -r requirements.txt
python app.py
```

Тесты:

```powershell
python -m unittest discover -s tests -v
```

## Локальная Windows-сборка

Для воспроизводимой сборки используется Python 3.12 x64, PyInstaller и
собственный `AperolSpritz.spec`:

```powershell
cd desktop
.\build.ps1
.\scripts\smoke_test_frozen.ps1
.\package_release.ps1 -Tag v1.0.0
```

Скрипт создаёт виртуальное окружение `.venv`, устанавливает runtime- и
build-зависимости, запускает тесты и собирает:

```text
desktop\dist\AperolSpritz\AperolSpritz.exe
desktop\release\AperolSpritz-Windows-x64.zip
desktop\release\AperolSpritz-Windows-x64.zip.sha256
```

Frozen smoke test использует временный `APPDATA`, инициализирует настоящий GUI,
проверяет создание `settings.json` и `measurements.db`, а затем завершает процесс
с тайм-аутом.

## Linux

Готовый Linux-бинарник в GitHub Releases не публикуется. На Debian/Ubuntu сначала
могут потребоваться системные пакеты:

```bash
sudo apt install python3 python3-venv python3-tk
git clone https://github.com/sakurelle/AperolSpritz.git
cd AperolSpritz/desktop
chmod +x build_linux.sh
./build_linux.sh
```

Результат: `desktop/dist/AperolSpritz/AperolSpritz`. Это нативная Linux onedir-
сборка; Windows EXE на Linux этим сценарием не создаётся. Пользовательские данные
сохраняются в `~/.aperolspritz`, если переменная `APPDATA` не задана.

## GitHub Actions и выпуск версии

Единственный источник версии desktop — [`VERSION`](VERSION).

```text
git tag vX.Y.Z
        ↓
Windows GitHub Actions
        ↓
PyInstaller onedir + frozen smoke test
        ↓
ZIP + SHA-256
        ↓
GitHub Release
```

Workflow `.github/workflows/release.yml` запускается для нового тега `v*` или
вручную для существующего тега. Он проверяет совпадение тега с `VERSION`, не
перезаписывает существующий Release и публикует только Windows ZIP и SHA-256.

## Если устройство недоступно

- проверьте IP и доступность `http://<IP>/` в той же сети;
- проверьте правила firewall для локальных HTTP-запросов;
- после восстановления сети приложение переподключится автоматически.

Недоступность одной установки не блокирует остальные и не замораживает GUI.
