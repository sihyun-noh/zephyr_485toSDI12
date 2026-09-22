#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_DIR="app/dajilog_scale"
PROFILE="${PROFILE:-profiles/scale_default.conf}"
BUILD_DIR="${BUILD_DIR:-build_dajilog_scale}"
ACTION="${1:-build}"

case "${ACTION}" in
build|flash) ;;
*)
	echo "Usage: $0 [build|flash]"
	echo "Optional: PROFILE=profiles/scale_default.conf BUILD_DIR=build_dajilog_scale"
	exit 2
	;;
esac

cd "${PROJECT_DIR}"
west build -p always -b dajilog_tech "${APP_DIR}" -d "${BUILD_DIR}" \
	-- -DEXTRA_CONF_FILE="${PROFILE}"

if [[ "${ACTION}" == "flash" ]]; then
	BUILD_DIR="${BUILD_DIR}" "${SCRIPT_DIR}/flash_with_usage.sh" -d "${BUILD_DIR}"
fi
