#!/usr/bin/env bash

set -euo pipefail

readonly OS_TOOLCHAIN_REQUIRED_TOOLS=(
  clang++
  ld.lld
  nasm
  qemu-system-x86_64
  gdb
  cmake
  make
  ctest
  llvm-nm
  llvm-objdump
  llvm-readelf
  grep
  sed
  truncate
  timeout
)

for requiredTool in "${OS_TOOLCHAIN_REQUIRED_TOOLS[@]}"; do
  if ! command -v "${requiredTool}" >/dev/null 2>&1; then
    printf '缺少必要工具：%s\n' "${requiredTool}" >&2
    exit 1
  fi
done

printf '工具链检查通过：\n'
printf '  Clang: %s\n' "$(clang++ --version | head -n 1)"
printf '  LLD:   %s\n' "$(ld.lld --version | head -n 1)"
printf '  NASM:  %s\n' "$(nasm --version | head -n 1)"
printf '  QEMU:  %s\n' "$(qemu-system-x86_64 --version | head -n 1)"
printf '  GDB:   %s\n' "$(gdb --version | head -n 1)"
printf '  CMake: %s\n' "$(cmake --version | head -n 1)"
