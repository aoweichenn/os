#!/usr/bin/env bash

set -euo pipefail

readonly OS_IMAGE_OUTPUT_DIRECTORY="${1:?缺少镜像输出目录}"
readonly OS_IMAGE_FIRMWARE_SIZE_BYTES="${2:?缺少固件镜像大小}"
readonly OS_IMAGE_DISK_SIZE_BYTES="${3:?缺少磁盘镜像大小}"
readonly OS_IMAGE_FIRMWARE_PATH="${OS_IMAGE_OUTPUT_DIRECTORY}/empty_firmware.bin"
readonly OS_IMAGE_DISK_PATH="${OS_IMAGE_OUTPUT_DIRECTORY}/empty_disk.img"

mkdir -p "${OS_IMAGE_OUTPUT_DIRECTORY}"
truncate --size="${OS_IMAGE_FIRMWARE_SIZE_BYTES}" "${OS_IMAGE_FIRMWARE_PATH}"
truncate --size="${OS_IMAGE_DISK_SIZE_BYTES}" "${OS_IMAGE_DISK_PATH}"
