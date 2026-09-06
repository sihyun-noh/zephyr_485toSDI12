#!/bin/sh
set -eu

PROFILE="${1:-sensor_default}"
APP_DIR="app/dajilog_485_to_sdi12"
PROFILE_FILE="${APP_DIR}/profiles/${PROFILE}.conf"
BUILD_DIR="build_dajilog_485_to_sdi12_${PROFILE#sensor_}"

if [ ! -f "${PROFILE_FILE}" ]; then
	echo "Profile not found: ${PROFILE_FILE}" >&2
	exit 1
fi

west build -p always \
	-b dajilog_tech \
	"${APP_DIR}" \
	-d "${BUILD_DIR}" \
	-- -DEXTRA_CONF_FILE="profiles/${PROFILE}.conf"
