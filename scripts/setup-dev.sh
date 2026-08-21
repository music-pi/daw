#!/usr/bin/env bash
#
# Prepare an Ubuntu/Debian checkout for MusicPI development.
#
# Usage:
#   ./scripts/setup-dev.sh [--skip-packages] [--skip-submodules] [--skip-cli]
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_PACKAGES=true
UPDATE_SUBMODULES=true
BUILD_CLI=true

usage() {
  cat <<'EOF'
Usage: ./scripts/setup-dev.sh [options]

Install development dependencies, initialise Git submodules, and build the
local mpi development CLI.

Options:
  --skip-packages    Do not install Ubuntu/Debian packages
  --skip-submodules  Do not sync or initialise Git submodules
  --skip-cli         Do not rebuild the mpi development CLI
  -h, --help         Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-packages)
      INSTALL_PACKAGES=false
      ;;
    --skip-submodules)
      UPDATE_SUBMODULES=false
      ;;
    --skip-cli)
      BUILD_CLI=false
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      printf 'Unknown option: %s\n\n' "$1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

log() {
  printf '==> %s\n' "$*"
}

if [[ ! -f "${ROOT_DIR}/.gitmodules" ]]; then
  printf 'Error: expected to find .gitmodules in %s\n' "${ROOT_DIR}" >&2
  exit 1
fi

if [[ "${INSTALL_PACKAGES}" == true ]]; then
  if ! command -v apt-get >/dev/null 2>&1; then
    printf '%s\n' \
      'Error: automatic package installation currently supports Ubuntu/Debian only.' \
      'Install the dependencies listed in README.md, then rerun with --skip-packages.' >&2
    exit 1
  fi

  APT=(apt-get)
  if [[ ${EUID} -ne 0 ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
      printf '%s\n' \
        'Error: sudo is required to install packages.' \
        'Install the dependencies manually, then rerun with --skip-packages.' >&2
      exit 1
    fi
    APT=(sudo apt-get)
  fi

  packages=(
    build-essential
    ca-certificates
    ccache
    cmake
    libasound2-dev
    libfreetype6-dev
    libgl1-mesa-dev
    libjack-jackd2-dev
    libusb-1.0-0-dev
    libx11-dev
    libxcursor-dev
    libxinerama-dev
    libxrandr-dev
    ninja-build
    pkg-config
    python3
    python3-venv
  )

  log "Installing development packages"
  "${APT[@]}" update
  "${APT[@]}" install --yes --no-install-recommends "${packages[@]}"
fi

if [[ "${UPDATE_SUBMODULES}" == true ]]; then
  if ! command -v git >/dev/null 2>&1; then
    printf 'Error: git is required to initialise submodules.\n' >&2
    exit 1
  fi

  log "Synchronising submodule URLs"
  git -C "${ROOT_DIR}" submodule sync --recursive

  log "Initialising submodules"
  git -C "${ROOT_DIR}" submodule update --init --recursive
fi

if [[ "${BUILD_CLI}" == true ]]; then
  log "Building the mpi development CLI"
  "${ROOT_DIR}/scripts/build-cli.sh"
fi

printf '\nDevelopment environment is ready.\n'
printf 'Next: ./mpi build headless && ./mpi test\n'
