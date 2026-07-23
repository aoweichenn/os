#!/usr/bin/env bash

set -euo pipefail

readonly OS_QEMU_FIRMWARE_IMAGE="${1:?缺少空固件镜像路径}"
readonly OS_QEMU_DISK_IMAGE="${2:?缺少空磁盘镜像路径}"
readonly OS_QEMU_EXPECTED_FIRMWARE_SIZE_BYTES="${3:?缺少固件镜像预期大小}"
readonly OS_QEMU_EXPECTED_DISK_SIZE_BYTES="${4:?缺少磁盘镜像预期大小}"
readonly OS_QEMU_ACTUAL_FIRMWARE_SIZE_BYTES="$(
  stat --format='%s' "${OS_QEMU_FIRMWARE_IMAGE}"
)"
readonly OS_QEMU_ACTUAL_DISK_SIZE_BYTES="$(
  stat --format='%s' "${OS_QEMU_DISK_IMAGE}"
)"
readonly OS_QEMU_SMOKE_TIMEOUT_SECONDS=2
readonly OS_QEMU_TIMEOUT_EXIT_STATUS=124
readonly OS_QEMU_GUEST_MEMORY_MEBIBYTES=64

if [[ "${OS_QEMU_ACTUAL_FIRMWARE_SIZE_BYTES}" != "${OS_QEMU_EXPECTED_FIRMWARE_SIZE_BYTES}" ]]; then
  printf '空固件镜像大小不正确：%s\n' \
    "${OS_QEMU_ACTUAL_FIRMWARE_SIZE_BYTES}" >&2
  exit 1
fi

if [[ "${OS_QEMU_ACTUAL_DISK_SIZE_BYTES}" != "${OS_QEMU_EXPECTED_DISK_SIZE_BYTES}" ]]; then
  printf '空磁盘镜像大小不正确：%s\n' \
    "${OS_QEMU_ACTUAL_DISK_SIZE_BYTES}" >&2
  exit 1
fi

set +e
timeout \
  --signal=TERM \
  "${OS_QEMU_SMOKE_TIMEOUT_SECONDS}s" \
  qemu-system-x86_64 \
  -machine pc,accel=tcg \
  -cpu qemu64 \
  -m "${OS_QEMU_GUEST_MEMORY_MEBIBYTES}" \
  -nodefaults \
  -display none \
  -serial none \
  -monitor none \
  -S \
  -no-reboot \
  -no-shutdown \
  -bios "${OS_QEMU_FIRMWARE_IMAGE}" \
  -drive "file=${OS_QEMU_DISK_IMAGE},format=raw,if=ide"
readonly OS_QEMU_ACTUAL_EXIT_STATUS=$?
set -e

if [[ "${OS_QEMU_ACTUAL_EXIT_STATUS}" -ne "${OS_QEMU_TIMEOUT_EXIT_STATUS}" ]]; then
  printf 'QEMU 硬件冒烟测试异常退出：%s\n' \
    "${OS_QEMU_ACTUAL_EXIT_STATUS}" >&2
  exit 1
fi

printf 'QEMU TCG 已使用自定义空固件与空磁盘稳定启动。\n'
