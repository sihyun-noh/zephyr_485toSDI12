#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"
ELF_FILE="${ELF_FILE:-${BUILD_DIR}/zephyr/zephyr.elf}"
FLASH_TOTAL_BYTES="${FLASH_TOTAL_BYTES:-131072}"
FLASH_BASE="${FLASH_BASE:-0x08000000}"
SIZE_TOOL="${SIZE_TOOL:-/Users/sihyun.noh/zephyr-sdk-0.16.8/arm-zephyr-eabi/bin/arm-zephyr-eabi-size}"
READELF_TOOL="${READELF_TOOL:-/Users/sihyun.noh/zephyr-sdk-0.16.8/arm-zephyr-eabi/bin/arm-zephyr-eabi-readelf}"

cd "${PROJECT_DIR}"

west flash "$@"

if [[ ! -f "${ELF_FILE}" ]]; then
	echo "FLASH usage: skipped, ELF not found: ${ELF_FILE}"
	exit 0
fi

flash_base=$((FLASH_BASE))
flash_limit=$((flash_base + FLASH_TOTAL_BYTES))
flash_end=${flash_base}

if [[ -x "${READELF_TOOL}" ]]; then
	while read -r type offset vaddr paddr filesz memsz _rest; do
		if [[ "${type}" != "LOAD" ]]; then
			continue
		fi

		phys=$((paddr))
		size=$((memsz))
		end=$((phys + size))

		if (( phys >= flash_base && phys < flash_limit && size > 0 && end > flash_end )); then
			flash_end=${end}
		fi
	done < <("${READELF_TOOL}" -l "${ELF_FILE}")
elif command -v arm-zephyr-eabi-readelf >/dev/null 2>&1; then
	READELF_TOOL="$(command -v arm-zephyr-eabi-readelf)"
	while read -r type offset vaddr paddr filesz memsz _rest; do
		if [[ "${type}" != "LOAD" ]]; then
			continue
		fi

		phys=$((paddr))
		size=$((memsz))
		end=$((phys + size))

		if (( phys >= flash_base && phys < flash_limit && size > 0 && end > flash_end )); then
			flash_end=${end}
		fi
	done < <("${READELF_TOOL}" -l "${ELF_FILE}")
fi

if (( flash_end > flash_base )); then
	flash_used=$((flash_end - flash_base))
else
	if [[ ! -x "${SIZE_TOOL}" ]]; then
		if command -v arm-zephyr-eabi-size >/dev/null 2>&1; then
			SIZE_TOOL="$(command -v arm-zephyr-eabi-size)"
		else
			echo "FLASH usage: skipped, readelf/size tool not found"
			exit 0
		fi
	fi

	read -r text_size data_size < <("${SIZE_TOOL}" "${ELF_FILE}" | awk 'NR == 2 {print $1, $2}')
	flash_used=$((text_size + data_size))
fi

awk -v used="${flash_used}" -v total="${FLASH_TOTAL_BYTES}" '
	BEGIN {
		printf("FLASH usage: %d B / %d B (%.2f%%)\n", used, total, (used * 100.0) / total)
	}
'
