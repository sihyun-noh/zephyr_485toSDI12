#!/bin/sh
set -eu

PROFILE="${1:-sensor_default}"
BUILD_DIR="build_dajilog_485_to_sdi12_${PROFILE#sensor_}"

west flash -d "${BUILD_DIR}"
