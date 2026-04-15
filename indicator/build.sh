#!/bin/bash
#* ******************************************************************************
#*  If not stated otherwise in this file or this component's LICENSE
#*  file the following copyright and licenses apply:
#*
#*  Copyright 2026 RDK Management
#*
#*  Licensed under the Apache License, Version 2.0 (the "License");
#*  you may not use this file except in compliance with the License.
#*  You may obtain a copy of the License at
#*
#*  http://www.apache.org/licenses/LICENSE-2.0
#*
#*  Unless required by applicable law or agreed to in writing, software
#*  distributed under the License is distributed on an "AS IS" BASIS,
#*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#*  See the License for the specific language governing permissions and
#*  limitations under the License.
#*
#* ******************************************************************************

# This script mirrors the TEVDevice workspace convention of providing a simple
# build.sh at the workspace root.
#
# It configures the *repo-level* CMake entrypoint and selects AIDL_TARGET=indicator.
# A prebuilt AIDL compiler can be supplied by exporting AIDL_BIN or passing it
# on the command line as AIDL_BIN=/path/to/aidl.
#
# Examples:
#   export AIDL_BIN=/path/to/prebuilt/aidl
#   ./build.sh
#
#   ./build.sh AIDL_BIN=/path/to/prebuilt/aidl
#
# Output:
#   - CMake build dir: ./build
#   - Generated AIDL artifacts: ../gen/indicator/current

set -euo pipefail

# Colors (kept minimal but similar spirit to TEVDevice)
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
RESET='\033[0m'

declare -r TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
declare -r REPO_ROOT="$(cd "${TOP}/.." && pwd)"

BUILD_DIR="${TOP}/build"
AIDL_SRC_VERSION="${AIDL_SRC_VERSION:-current}"
AIDL_TARGET="${AIDL_TARGET:-indicator}"

# Allow overriding AIDL_BIN via CLI: ./build.sh AIDL_BIN=/path/to/aidl
for arg in "$@"; do
  case "${arg}" in
    AIDL_BIN=*)
      export AIDL_BIN="${arg#*=}"
      ;;
    AIDL_SRC_VERSION=*)
      AIDL_SRC_VERSION="${arg#*=}"
      ;;
    AIDL_TARGET=*)
      AIDL_TARGET="${arg#*=}"
      ;;
    help|-h|--help)
      cat <<EOF
Usage:
  $0 [AIDL_BIN=/path/to/aidl] [AIDL_SRC_VERSION=current] [AIDL_TARGET=indicator]

Environment variables:
  AIDL_BIN          Path to prebuilt 'aidl' compiler (required if not on PATH)
  AIDL_SRC_VERSION  AIDL source version directory (default: current)
  AIDL_TARGET       Module name (default: indicator)

This script configures and builds the repository root CMake project:
  cmake -S <repo_root> -B <indicator>/build -DAIDL_TARGET=... -DAIDL_SRC_VERSION=... [-DAIDL_BIN=...]

EOF
      exit 0
      ;;
    *)
      echo -e "${BOLD}${RED}Unknown argument: ${arg}${RESET}"
      echo "Run: $0 --help"
      exit 2
      ;;
  esac
done

mkdir -p "${BUILD_DIR}"

if [[ -n "${AIDL_BIN:-}" ]]; then
  if [[ ! -x "${AIDL_BIN}" ]]; then
    echo -e "${BOLD}${RED}AIDL_BIN is set but not executable: ${AIDL_BIN}${RESET}"
    exit 1
  fi
fi

echo -e "${BOLD}${GREEN}Config:${RESET}"
echo -e "  ${YELLOW}REPO_ROOT      :${RESET} ${REPO_ROOT}"
echo -e "  ${YELLOW}BUILD_DIR       :${RESET} ${BUILD_DIR}"
echo -e "  ${YELLOW}AIDL_TARGET     :${RESET} ${AIDL_TARGET}"
echo -e "  ${YELLOW}AIDL_SRC_VERSION:${RESET} ${AIDL_SRC_VERSION}"
echo -e "  ${YELLOW}AIDL_BIN        :${RESET} ${AIDL_BIN:-<not set>}"

CMAKE_ARGS=(
  -S "${REPO_ROOT}"
  -B "${BUILD_DIR}"
  -DAIDL_TARGET="${AIDL_TARGET}"
  -DAIDL_SRC_VERSION="${AIDL_SRC_VERSION}"
)

# Pass AIDL_BIN into CMake if provided, otherwise allow CompileAidl.cmake to
# resolve it from PATH (user may have aidl available already).
if [[ -n "${AIDL_BIN:-}" ]]; then
  CMAKE_ARGS+=(-DAIDL_BIN="${AIDL_BIN}")
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo -e "${BOLD}${GREEN}Build completed successfully.${RESET}"
echo -e "${BOLD}${GREEN}Generated output (default):${RESET} ${REPO_ROOT}/gen/${AIDL_TARGET}/${AIDL_SRC_VERSION}"
