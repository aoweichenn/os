#!/usr/bin/env bash

set -euo pipefail

readonly OS_SCRIPT_DIRECTORY="$(
  cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1
  pwd
)"
readonly OS_PROJECT_ROOT="$(
  cd -- "${OS_SCRIPT_DIRECTORY}/.." >/dev/null 2>&1
  pwd
)"

"${OS_SCRIPT_DIRECTORY}/check_toolchain.sh"
cmake --preset developer -S "${OS_PROJECT_ROOT}"
cmake --build --preset developer
ctest --preset developer
