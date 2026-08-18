#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.."; pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
CONFIG="${TYPE:-Release}"
EXECUTABLE="MaschinePI"
GDB_FLAG=

if [[ "${1:-}" == "--gdb" ]]; then
  CONFIG="Debug"
  GDB_FLAG="gdb --args"
  shift
  "${ROOT_DIR}/scripts/build.sh" --debug
elif [[ "${1:-}" == "--debug" ]]; then
  CONFIG="Debug"
  shift
fi

if [[ ! -d "${ROOT_DIR}/${BUILD_DIR}" ]]; then
  echo "[run.sh] Build directory '${BUILD_DIR}' does not exist. Run scripts/build.sh first." >&2
  exit 1
fi

cd "${ROOT_DIR}/${BUILD_DIR}"

BIN="./maschinepi"
if [[ ! -x "${BIN}" ]]; then
  ALT1="./maschinepi_artefacts/${CONFIG}/${EXECUTABLE}"
  ALT2="./maschinefpi_artefacts/${CONFIG}/Standalone/${EXECUTABLE}"
  if [[ -x "${ALT1}" ]]; then
    BIN="${ALT1}"
  elif [[ -x "${ALT2}" ]]; then
    BIN="${ALT2}"
  else
    echo "Could not find executable. Checked ${BIN}, ${ALT1}, ${ALT2}." >&2
    exit 2
  fi
fi

echo "==> Running ${BIN} (config: ${CONFIG})"
pw-jack ${GDB_FLAG} "${BIN}" "$@"