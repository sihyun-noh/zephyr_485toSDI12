#!/bin/zsh
set -e
cd "${0:A:h}"
if [[ ! -x .venv/bin/dajilog-provisioner ]]; then
  echo "먼저 ./install.sh을 실행하세요."
  exit 1
fi
exec .venv/bin/dajilog-provisioner

