#!/bin/zsh
set -e
cd "${0:A:h}"
python3.12 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/pip install -e '.[dev]'
.venv/bin/pytest
