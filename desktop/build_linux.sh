#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

python_bin="${PYTHON:-python3}"
command -v "$python_bin" >/dev/null 2>&1 || { echo "Python 3 is required" >&2; exit 1; }
"$python_bin" -c 'import sys; assert sys.version_info >= (3, 9), sys.version'

if [[ ! -x .venv/bin/python ]]; then
  "$python_bin" -m venv .venv
fi
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r requirements.txt -r requirements-build.txt
.venv/bin/python -m compileall -q app.py aperolspritz tests
.venv/bin/python -m unittest discover -s tests -v
.venv/bin/python -m PyInstaller --noconfirm --clean AperolSpritz.spec
cp README.release.txt dist/AperolSpritz/README.txt
test -x dist/AperolSpritz/AperolSpritz
test -d dist/AperolSpritz/_internal
printf 'Linux onedir build: %s\n' "$script_dir/dist/AperolSpritz/AperolSpritz"
