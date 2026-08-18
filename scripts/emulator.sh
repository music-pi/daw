#!/usr/bin/env bash
#
# MK3 Emulator management
#
# Usage:
#   scripts/emulator.sh run [--no-build]          Start emulator + build + run app
#   scripts/emulator.sh qa [--build] [--no-app]   Headless emulator for QA testing
#   scripts/emulator.sh test                       Run emulator parity tests
#
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.."; pwd)"

readonly BUILD_DIR="${BUILD_DIR:-build}"

# Colors
if [[ -t 1 ]]; then
  RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
  BLUE='\033[0;34m'; BOLD='\033[1m'; NC='\033[0m'
else
  RED='' GREEN='' YELLOW='' BLUE='' BOLD='' NC=''
fi
log_info()    { echo -e "${BLUE}==> ${NC}$*"; }
log_success() { echo -e "${GREEN}==> ${NC}$*"; }
log_warn()    { echo -e "${YELLOW}==> WARNING: ${NC}$*"; }
log_error()   { echo -e "${RED}==> ERROR: ${NC}$*" >&2; }

setup_emulator_venv() {
  local emu_dir="${ROOT_DIR}/tools/mk3-emulator"
  local venv_dir="${emu_dir}/.venv"
  local requirements_stamp="${venv_dir}/.requirements.stamp"

  if [[ ! -d "${venv_dir}" ]]; then
    log_info "Creating Python virtual environment..."

    if ! command -v python3 >/dev/null 2>&1; then
      log_error "python3 not found. Please install Python 3."
      exit 1
    fi

    python3 -m venv "${venv_dir}"
    log_info "Installing dependencies..."
    "${venv_dir}/bin/pip" install --quiet --upgrade pip
    "${venv_dir}/bin/pip" install --quiet -r "${emu_dir}/requirements.txt"
    touch "${requirements_stamp}"
    log_success "Emulator environment ready"
  fi

  if [[ ! -f "${requirements_stamp}" || "${emu_dir}/requirements.txt" -nt "${requirements_stamp}" ]]; then
    log_info "Updating emulator dependencies..."
    "${venv_dir}/bin/pip" install --quiet -r "${emu_dir}/requirements.txt"
    touch "${requirements_stamp}"
  fi
}

find_binary() {
  local bin=""
  local search_paths=(
    "${ROOT_DIR}/${BUILD_DIR}/bin/maschinepi"
    "${ROOT_DIR}/${BUILD_DIR}/maschinepi"
    "${ROOT_DIR}/${BUILD_DIR}/maschinepi_artefacts/Release/maschinepi"
    "${ROOT_DIR}/${BUILD_DIR}/maschinepi_artefacts/Debug/maschinepi"
    "${ROOT_DIR}/${BUILD_DIR}/maschinepi_artefacts/Release/Standalone/maschinepi"
  )
  for path in "${search_paths[@]}"; do
    if [[ -x "${path}" ]]; then
      echo "${path}"
      return 0
    fi
  done
  return 1
}

do_run_emulator() {
  local skip_build=false
  for arg in "$@"; do
    if [[ "${arg}" == "--no-build" ]]; then
      skip_build=true
    fi
  done

  local emu_dir="${ROOT_DIR}/tools/mk3-emulator"
  local venv_dir="${emu_dir}/.venv"

  setup_emulator_venv

  log_info "Starting MK3 emulator (TCP :9999, HTTP API :9998)"
  "${venv_dir}/bin/python3" "${emu_dir}/emulator.py" &
  local emu_pid=$!

  trap "kill ${emu_pid} 2>/dev/null; wait ${emu_pid} 2>/dev/null" EXIT

  log_info "Waiting for emulator to start..."
  local retries=0
  # Probe the HTTP control plane. Connecting to the controller TCP port would
  # impersonate the app and create a fake controller connect/disconnect cycle.
  while ! curl -fsS http://127.0.0.1:9998/health >/dev/null 2>&1; do
    sleep 0.2
    retries=$((retries + 1))
    if [[ ${retries} -ge 25 ]]; then
      log_error "Emulator failed to start (timeout after 5s)"
      exit 1
    fi
    if ! kill -0 ${emu_pid} 2>/dev/null; then
      log_error "Emulator process exited unexpectedly"
      exit 1
    fi
  done
  log_success "Emulator ready"

  if [[ "${skip_build}" == false ]]; then
    "${ROOT_DIR}/mpi" build headless --emu
  fi

  local bin
  bin=$(find_binary) || {
    log_error "Could not find maschinepi binary."
    echo "  Have you run './mpi build headless --emu' first?"
    exit 2
  }

  log_info "Running ${bin} (Ctrl+C to stop both app and emulator)"
  if command -v pw-jack >/dev/null 2>&1; then
    pw-jack "${bin}"
  else
    "${bin}"
  fi
}

do_qa_emulator() {
  local emu_dir="${ROOT_DIR}/tools/mk3-emulator"
  local venv_dir="${emu_dir}/.venv"
  local do_build=false
  local skip_app=false

  for arg in "$@"; do
    case "${arg}" in
      --build) do_build=true ;;
      --no-app) skip_app=true ;;
    esac
  done

  setup_emulator_venv

  log_info "Starting MK3 emulator (headless mode)"
  "${venv_dir}/bin/python3" "${emu_dir}/emulator.py" --headless &
  local emu_pid=$!

  trap "kill ${emu_pid} 2>/dev/null; [[ -n \${app_pid:-} ]] && kill \${app_pid} 2>/dev/null; wait 2>/dev/null" EXIT

  log_info "Waiting for emulator..."
  local retries=0
  while ! curl -fsS http://127.0.0.1:9998/health >/dev/null 2>&1; do
    sleep 0.2
    retries=$((retries + 1))
    if [[ ${retries} -ge 25 ]]; then
      log_error "Emulator failed to start (timeout after 5s)"
      exit 1
    fi
    if ! kill -0 ${emu_pid} 2>/dev/null; then
      log_error "Emulator process exited unexpectedly"
      exit 1
    fi
  done
  log_success "Emulator ready (TCP :9999, HTTP :9998)"

  if [[ "${do_build}" == true ]]; then
    "${ROOT_DIR}/mpi" build headless --emu
  fi

  if [[ "${skip_app}" == true ]]; then
    log_success "QA emulator running (no app). PID: ${emu_pid}"
    echo ""
    echo "  Health:     curl localhost:9998/health"
    echo "  Screenshot: curl localhost:9998/screenshot -o /tmp/mpi-screen.png"
    echo ""
    echo "  Press Ctrl+C to stop."
    wait ${emu_pid}
    return
  fi

  local bin
  bin=$(find_binary) || {
    log_error "Could not find maschinepi binary."
    echo "  Run './mpi build headless --emu' first, or use --build flag."
    exit 2
  }

  log_info "Starting app in background..."
  local app_pid
  if command -v pw-jack >/dev/null 2>&1; then
    pw-jack "${bin}" &
  else
    "${bin}" &
  fi
  app_pid=$!

  log_info "Waiting for app to connect..."
  retries=0
  while true; do
    local health
    health=$(curl -s localhost:9998/health 2>/dev/null || echo '{}')
    if echo "${health}" | grep -q '"connected"'; then
      if echo "${health}" | grep -q 'true'; then
        break
      fi
    fi
    sleep 0.5
    retries=$((retries + 1))
    if [[ ${retries} -ge 30 ]]; then
      log_error "App failed to connect to emulator (timeout after 15s)"
      exit 1
    fi
    if ! kill -0 ${app_pid} 2>/dev/null; then
      log_error "App process exited unexpectedly"
      exit 1
    fi
  done

  log_success "QA environment ready!"
  echo ""
  echo "  Emulator PID: ${emu_pid}"
  echo "  App PID:      ${app_pid}"
  echo ""
  echo "  Endpoints:"
  echo "    Health:     curl localhost:9998/health"
  echo "    Screenshot: curl localhost:9998/screenshot -o /tmp/mpi-screen.png"
  echo "    State:      curl localhost:9998/state"
  echo "    Button:     curl -X POST localhost:9998/button -d '{\"name\":\"play\",\"pressed\":true}'"
  echo "    Pad:        curl -X POST localhost:9998/pad -d '{\"index\":1,\"pressure\":4000}'"
  echo ""
  echo "  Press Ctrl+C to stop both."
  wait ${app_pid} ${emu_pid}
}

do_test_emulator() {
  local emu_dir="${ROOT_DIR}/tools/mk3-emulator"
  local venv_dir="${emu_dir}/.venv"

  setup_emulator_venv
  log_info "Running MK3 emulator parity tests"
  (
    cd "${emu_dir}"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "${venv_dir}/bin/python3" -m unittest -v test_emulator.py
  )
}

# Main dispatch
mode="${1:-}"
shift || true

case "${mode}" in
  run)  do_run_emulator "$@" ;;
  qa)   do_qa_emulator "$@" ;;
  test) do_test_emulator "$@" ;;
  *)
    echo "Usage: $(basename "$0") run [--no-build]"
    echo "       $(basename "$0") qa [--build] [--no-app]"
    echo "       $(basename "$0") test"
    exit 1
    ;;
esac
