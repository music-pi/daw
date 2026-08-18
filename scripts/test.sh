#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.."; pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
TYPE="${TYPE:-Debug}"
COVERAGE_ENABLED=1
SKIP_BUILD=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --skip-build       Run tests/coverage without invoking scripts/build.sh
  --skip-coverage    Skip coverage generation (just build + run ctest)
  --coverage         Force coverage on (default)
  -h, --help         Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build) SKIP_BUILD=1; shift ;;
    --skip-coverage) COVERAGE_ENABLED=0; shift ;;
    --coverage) COVERAGE_ENABLED=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

ensure_build() {
  if [[ "${SKIP_BUILD}" -eq 1 ]]; then
    return
  fi

  echo "==> Building project (TYPE=${TYPE}, COVERAGE=$([[ $COVERAGE_ENABLED -eq 1 ]] && echo ON || echo OFF))"
  local cmake_args=()
  if [[ "${COVERAGE_ENABLED}" -eq 1 ]]; then
    cmake_args+=("-DENABLE_COVERAGE=ON")
  fi

  (cd "${ROOT_DIR}" && \
    CMAKE_ARGS="${CMAKE_ARGS:-} ${cmake_args[*]}" \
    BUILD_DIR="${BUILD_DIR}" \
    TYPE="${TYPE}" \
    ./scripts/build.sh --build-dir "${BUILD_DIR}" --type "${TYPE}")
}

ensure_coverage_config() {
  if [[ "${COVERAGE_ENABLED}" -ne 1 ]]; then
    return
  fi

  local cache="${ROOT_DIR}/${BUILD_DIR}/CMakeCache.txt"
  if [[ ! -f "${cache}" ]]; then
    return
  fi

  if ! grep -q "ENABLE_COVERAGE:BOOL=ON" "${cache}"; then
    echo "Coverage requested but build dir '${BUILD_DIR}' is not configured with ENABLE_COVERAGE=ON. Skipping coverage report." >&2
    COVERAGE_ENABLED=0
  fi
}

run_tests() {
  cd "${ROOT_DIR}/${BUILD_DIR}"

  CTEST_ARGS=()
  if [[ -n "${CTEST_PARALLEL_LEVEL:-}" ]]; then
    CTEST_ARGS+=("-j" "${CTEST_PARALLEL_LEVEL}")
  fi

  if ! command -v ctest >/dev/null 2>&1; then
    echo "ctest command not found. Please install CMake/CTest." >&2
    exit 2
  fi

  echo "==> Running ctest ${CTEST_ARGS[*]}"
  if [[ -n "${GTEST_FILTER:-}" ]]; then
    GTEST_FILTER="${GTEST_FILTER}" ctest --output-on-failure "${CTEST_ARGS[@]}"
  else
    ctest --output-on-failure "${CTEST_ARGS[@]}"
  fi
}

run_coverage() {
  if [[ "${COVERAGE_ENABLED}" -ne 1 ]]; then
    return
  fi

  if ! command -v gcovr >/dev/null 2>&1; then
    echo "gcovr not found; skipping coverage report." >&2
    return
  fi

  cd "${ROOT_DIR}/${BUILD_DIR}"
  echo "==> Generating coverage report"
  gcovr -r "${ROOT_DIR}" \
        --filter "${ROOT_DIR}/src" \
        --exclude "${ROOT_DIR}/external" \
        --exclude "${ROOT_DIR}/tests" \
        --gcov-ignore-errors=no_working_dir_found \
        --gcov-ignore-errors=source_not_found \
        --print-summary
}

if [[ ! -d "${ROOT_DIR}/${BUILD_DIR}" ]]; then
  mkdir -p "${ROOT_DIR}/${BUILD_DIR}"
fi

ensure_build
ensure_coverage_config
run_tests
run_coverage