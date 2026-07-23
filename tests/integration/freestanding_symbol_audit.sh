#!/usr/bin/env bash

set -euo pipefail

readonly OS_AUDIT_LIBRARY_PATH="${1:?缺少待检查的 freestanding 库路径}"
readonly OS_AUDIT_EXPECTED_MACHINE='Advanced Micro Devices X86-64'
readonly OS_AUDIT_UNDEFINED_SYMBOLS="$(
  llvm-nm --undefined-only "${OS_AUDIT_LIBRARY_PATH}" |
    sed --expression='/^[[:space:]]*$/d' --expression='/:$/d'
)"
readonly OS_AUDIT_FILE_HEADER="$(
  llvm-readelf --file-header "${OS_AUDIT_LIBRARY_PATH}"
)"

if [[ -n "${OS_AUDIT_UNDEFINED_SYMBOLS}" ]]; then
  printf '发现未解析运行时符号：\n%s\n' "${OS_AUDIT_UNDEFINED_SYMBOLS}" >&2
  exit 1
fi

if ! grep --fixed-strings --quiet \
  "${OS_AUDIT_EXPECTED_MACHINE}" <<<"${OS_AUDIT_FILE_HEADER}"; then
  printf 'freestanding 产物不是 x86-64 ELF。\n' >&2
  exit 1
fi

printf 'freestanding 符号审计通过：%s\n' "${OS_AUDIT_LIBRARY_PATH}"
