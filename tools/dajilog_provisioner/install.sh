#!/bin/zsh
set -e
cd "${0:A:h}"

if [[ -x /opt/homebrew/bin/python3.12 ]]; then
  PYTHON=/opt/homebrew/bin/python3.12
elif [[ -x /usr/local/bin/python3.12 ]]; then
  PYTHON=/usr/local/bin/python3.12
elif command -v python3.12 >/dev/null 2>&1; then
  PYTHON=python3.12
else
  echo "Python 3.12가 필요합니다. Homebrew에서 python@3.12를 설치하세요."
  exit 1
fi

if [[ -x .venv/bin/python ]] && ! .venv/bin/python -c \
  'import sys; raise SystemExit(0 if sys.version_info[:2] == (3, 12) else 1)'; then
  echo "기존 .venv를 Python 3.12 환경으로 다시 구성합니다."
  "$PYTHON" -m venv --clear .venv
else
  "$PYTHON" -m venv .venv
fi
.venv/bin/python -m pip install --upgrade pip
.venv/bin/pip install -e '.[dev]'
.venv/bin/pytest
