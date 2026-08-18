#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.."; pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
CONFIG="${TYPE:-Release}"

if [[ ! -d "${ROOT_DIR}/${BUILD_DIR}" ]]; then
  echo "[run-tests.sh] Build directory '${BUILD_DIR}' does not exist. Run scripts/build.sh first." >&2
  exit 1
fi

cd "${ROOT_DIR}/${BUILD_DIR}"

BIN="./maschinepi_tests"
if [[ ! -x "${BIN}" ]]; then
  ALT="./maschinepi_tests_artefacts/${CONFIG}/maschinepi_tests"
  if [[ -x "${ALT}" ]]; then
    BIN="${ALT}"
  else
    echo "Could not find built test binary. Expected ${BIN} or ${ALT}." >&2
    exit 2
  fi
fi

echo "==> Running ${BIN}"
"${BIN}"
