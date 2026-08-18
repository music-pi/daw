#!/usr/bin/env bash
#
# MaschinePI - Unified Build/Run/Test CLI
#
# Usage:
#   ./mpi                    # Interactive menu
#   ./mpi build [target]     # Build (desktop|pi|tools)
#   ./mpi run                # Run desktop version
#   ./mpi test               # Run tests
#   ./mpi image              # Create Pi image
#   ./mpi clean              # Clean build artifacts
#
set -euo pipefail

# =================== Constants & Colors ===================
readonly VERSION="1.0.0"
readonly ROOT_DIR="$(cd "$(dirname "$0")"; pwd)"
readonly BUILD_DIR="${BUILD_DIR:-build}"
readonly BUILD_PI_DIR="${BUILD_PI_DIR:-build-pi}"

# Colors (disable if not a terminal)
if [[ -t 1 ]]; then
  readonly RED='\033[0;31m'
  readonly GREEN='\033[0;32m'
  readonly YELLOW='\033[1;33m'
  readonly BLUE='\033[0;34m'
  readonly CYAN='\033[0;36m'
  readonly BOLD='\033[1m'
  readonly NC='\033[0m' # No Color
else
  readonly RED='' GREEN='' YELLOW='' BLUE='' CYAN='' BOLD='' NC=''
fi

# =================== Helper Functions ===================
log_info()    { echo -e "${BLUE}==> ${NC}$*"; }
log_success() { echo -e "${GREEN}==> ${NC}$*"; }
log_warn()    { echo -e "${YELLOW}==> WARNING: ${NC}$*"; }
log_error()   { echo -e "${RED}==> ERROR: ${NC}$*" >&2; }

print_header() {
  echo -e "${BOLD}"
  echo "  __  __            _     _            ____ ___ "
  echo " |  \/  | __ _  ___| |__ (_)_ __   ___|  _ \_ _|"
  echo " | |\/| |/ _\` |/ __| '_ \| | '_ \ / _ \ |_) | | "
  echo " | |  | | (_| |\__ \ | | | | | | |  __/  __/| | "
  echo " |_|  |_|\__,_||___/_| |_|_|_| |_|\___|_|  |___|"
  echo -e "${NC}"
  echo "  MaschinePI Build System v${VERSION}"
  echo ""
}

# =================== Build Functions ===================
setup_ccache() {
  if command -v ccache >/dev/null 2>&1; then
    export CCACHE_BASEDIR="${ROOT_DIR}"
    export CCACHE_DIR="${CCACHE_DIR:-${ROOT_DIR}/.ccache}"
    export CCACHE_TEMPDIR="${CCACHE_TEMPDIR:-${CCACHE_DIR}/tmp}"
    export CCACHE_COMPRESS="${CCACHE_COMPRESS:-1}"
    export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-10G}"
    mkdir -p "${CCACHE_DIR}" "${CCACHE_TEMPDIR}"
    ccache -o cache_dir="${CCACHE_DIR}" >/dev/null 2>&1 || true
    ccache -M "${CCACHE_MAXSIZE}" >/dev/null 2>&1 || true
    echo "-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
  fi
}

# Install ARM64 cross-compilation dependencies
install_arm64_deps() {
  log_info "Installing ARM64 cross-compilation dependencies..."
  echo ""
  echo "  This will install:"
  echo "    - gcc-aarch64-linux-gnu (C cross-compiler)"
  echo "    - g++-aarch64-linux-gnu (C++ cross-compiler)"
  echo "    - libusb-1.0-0-dev:arm64 (USB library for ARM64)"
  echo "    - libasound2-dev:arm64 (ALSA library for ARM64)"
  echo "    - libfreetype-dev:arm64 (Font rendering for ARM64)"
  echo "    - libfontconfig1-dev:arm64 (Font configuration for ARM64)"
  echo ""

  # Check for apt
  if ! command -v apt >/dev/null 2>&1; then
    log_error "apt not found. This command only works on Debian/Ubuntu systems."
    exit 1
  fi

  # Need sudo
  if [[ "$EUID" -ne 0 ]]; then
    log_info "Requesting sudo access..."
  fi

  # Enable ARM64 architecture
  log_info "Enabling ARM64 architecture..."
  sudo dpkg --add-architecture arm64

  # Get Ubuntu codename
  local codename
  codename=$(lsb_release -cs 2>/dev/null || echo "noble")

  # ARM64 packages are on ports.ubuntu.com, not the main archive
  # Remove any conflicting ARM64 sources files first
  log_info "Configuring ARM64 package sources (ports.ubuntu.com)..."
  sudo rm -f /etc/apt/sources.list.d/arm64-ports.list* 2>/dev/null || true
  sudo rm -f /etc/apt/sources.list.d/arm64-ports.sources* 2>/dev/null || true
  sudo rm -f /etc/apt/sources.list.d/arm64-cross.sources* 2>/dev/null || true

  # Create a clean sources file for arm64
  sudo tee /etc/apt/sources.list.d/arm64-cross.list > /dev/null << EOF
# ARM64 cross-compilation packages from Ubuntu ports
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports ${codename} main universe
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports ${codename}-updates main universe
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports ${codename}-security main universe
EOF

  # Update package lists
  log_info "Updating package lists..."
  sudo apt update

  # Install cross-compilers (native packages)
  log_info "Installing cross-compilers..."
  sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

  # Install ARM64 libraries
  log_info "Installing ARM64 libraries..."
  sudo apt install -y \
    libusb-1.0-0-dev:arm64 \
    libasound2-dev:arm64 \
    libfreetype-dev:arm64 \
    libfontconfig1-dev:arm64

  echo ""
  log_success "ARM64 cross-compilation dependencies installed!"
  echo ""
  echo "  You can now run: ./mpi build pi"
  echo ""
}

# Check if ARM64 cross-compilation toolchain is available
check_arm64_toolchain() {
  local check_only="${1:-0}"
  local missing_compiler=()
  local missing_libs=()

  # Check compilers
  if ! command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    missing_compiler+=("aarch64-linux-gnu-gcc")
  fi
  if ! command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
    missing_compiler+=("aarch64-linux-gnu-g++")
  fi

  # Check for ARM64 libraries (multiarch)
  if [[ ! -d "/usr/lib/aarch64-linux-gnu/pkgconfig" ]]; then
    missing_libs+=("ARM64 pkgconfig directory")
  fi
  if [[ ! -f "/usr/lib/aarch64-linux-gnu/pkgconfig/libusb-1.0.pc" ]]; then
    missing_libs+=("libusb-1.0-0-dev:arm64")
  fi
  if [[ ! -f "/usr/lib/aarch64-linux-gnu/pkgconfig/freetype2.pc" ]]; then
    missing_libs+=("libfreetype-dev:arm64")
  fi

  local has_errors=0

  if [[ ${#missing_compiler[@]} -gt 0 ]]; then
    has_errors=1
    log_error "ARM64 cross-compiler not found"
    echo ""
    echo "  Missing: ${missing_compiler[*]}"
    echo ""
    echo "  Install with:"
    echo "    sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
    echo ""
  fi

  if [[ ${#missing_libs[@]} -gt 0 ]]; then
    has_errors=1
    log_error "ARM64 libraries not found"
    echo ""
    echo "  Missing: ${missing_libs[*]}"
    echo ""
    echo "  Enable multiarch and install ARM64 libraries:"
    echo "    sudo dpkg --add-architecture arm64"
    echo "    sudo apt update"
    echo "    sudo apt install libusb-1.0-0-dev:arm64 libasound2-dev:arm64"
    echo ""
  fi

  if [[ "${has_errors}" == "1" ]]; then
    echo "  Or run: ./mpi build pi --install-deps"
    echo ""
    if [[ "${check_only}" == "1" ]]; then
      return 1
    fi
    exit 1
  fi

  if [[ "${check_only}" == "1" ]]; then
    log_success "ARM64 cross-compilation environment ready"
    echo ""
    echo "  C:      $(command -v aarch64-linux-gnu-gcc)"
    echo "  C++:    $(command -v aarch64-linux-gnu-g++)"
    echo "  libusb: /usr/lib/aarch64-linux-gnu/pkgconfig/libusb-1.0.pc"
    return 0
  fi
}

detect_generator() {
  if command -v ninja >/dev/null 2>&1; then
    echo "Ninja"
  else
    echo "Unix Makefiles"
  fi
}

do_build() {
  # Separate --emu flag from positional args
  local positional=()
  local emulator_mode="OFF"
  for arg in "$@"; do
    if [[ "${arg}" == "--emu" ]]; then
      emulator_mode="ON"
    else
      positional+=("${arg}")
    fi
  done

  local target="${positional[0]:-headless}"
  local build_type="${positional[1]:-Release}"
  local dev_desktop="ON"
  local build_tools="OFF"
  local extra_args=""
  local use_toolchain=""
  local actual_build_dir="${BUILD_DIR}"

  case "${target}" in
    desktop)
      dev_desktop="ON"
      log_info "Building for desktop (GUI enabled)"
      ;;
    pi|raspberry)
      dev_desktop="OFF"
      # Check for --check flag
      if [[ "${build_type}" == "--check" ]]; then
        check_arm64_toolchain 1
        return $?
      fi
      # Check for --install-deps flag
      if [[ "${build_type}" == "--install-deps" ]]; then
        install_arm64_deps
        return $?
      fi
      check_arm64_toolchain
      use_toolchain="${ROOT_DIR}/pi-tools/aarch64-toolchain.cmake"
      actual_build_dir="${BUILD_PI_DIR}"
      log_info "Cross-compiling for Raspberry Pi (ARM64)"
      ;;
    headless)
      dev_desktop="OFF"
      log_info "Building for headless (native)"
      ;;
    tools)
      build_tools="ON"
      log_info "Building with tools enabled"
      ;;
    debug)
      build_type="Debug"
      log_info "Building debug configuration"
      ;;
    release)
      build_type="Release"
      log_info "Building release configuration"
      ;;
    *)
      log_error "Unknown build target: ${target}"
      echo "  Valid targets: desktop, pi, headless, tools, debug, release"
      exit 1
      ;;
  esac

  if [[ "${emulator_mode}" == "ON" ]]; then
    log_info "Emulator mode: using socket emulator instead of USB hardware"
  fi

  local generator
  generator="$(detect_generator)"
  local ccache_args
  ccache_args="$(setup_ccache)"

  mkdir -p "${ROOT_DIR}/${actual_build_dir}"
  cd "${ROOT_DIR}/${actual_build_dir}"

  local toolchain_args=""
  if [[ -n "${use_toolchain}" ]]; then
    toolchain_args="-DCMAKE_TOOLCHAIN_FILE=${use_toolchain}"
    log_info "Using toolchain: ${use_toolchain}"
  fi

  log_info "Configuring (generator: ${generator})"
  cmake \
    -G "${generator}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DDEV_DESKTOP="${dev_desktop}" \
    -DENABLE_TRACKTION=ON \
    -DBUILD_TOOLS="${build_tools}" \
    -DEMULATOR_MODE="${emulator_mode}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    ${toolchain_args} \
    ${ccache_args} \
    ${extra_args} \
    "${ROOT_DIR}"

  log_info "Building (${build_type})"
  cmake --build . -j"$(nproc)"

  echo ""
  log_success "Build complete!"
  echo ""
  echo "  Target:     ${target}"
  echo "  Build type: ${build_type}"
  echo "  Output:     ${ROOT_DIR}/${actual_build_dir}/bin/"

  # For Pi builds, also build mk3_cli utility
  if [[ "${target}" == "pi" || "${target}" == "raspberry" ]]; then
    echo ""
    log_info "Building mk3_cli for Pi..."

    # Build mk3_cli using the pi-tools CMakeLists.txt
    local utils_build_dir="${ROOT_DIR}/${actual_build_dir}/mk3-utils"
    mkdir -p "${utils_build_dir}"
    cd "${utils_build_dir}"

    cmake \
      -G "${generator}" \
      -DCMAKE_BUILD_TYPE="${build_type}" \
      -DCMAKE_TOOLCHAIN_FILE="${use_toolchain}" \
      ${ccache_args} \
      "${ROOT_DIR}/pi-tools"

    cmake --build . -j"$(nproc)"

    # Copy mk3_cli to main bin directory
    if [[ -f "${utils_build_dir}/mk3_cli" ]]; then
      cp "${utils_build_dir}/mk3_cli" "${ROOT_DIR}/${actual_build_dir}/bin/"
      log_success "Built mk3_cli"
    fi

    echo ""
    log_success "Pi build complete!"
    echo ""
    echo "  Binaries in ${ROOT_DIR}/${actual_build_dir}/bin/:"
    ls -la "${ROOT_DIR}/${actual_build_dir}/bin/" 2>/dev/null || true
  fi
  echo ""

  # Show ccache stats if available
  if command -v ccache >/dev/null 2>&1; then
    ccache -s 2>/dev/null | head -5 || true
  fi
}

# =================== Run Function ===================
do_run() {
  local debug_mode="${1:-}"
  local gdb_prefix=""

  if [[ "${debug_mode}" == "--gdb" ]]; then
    gdb_prefix="gdb --args"
    log_info "Running with GDB debugger"
  fi

  cd "${ROOT_DIR}/${BUILD_DIR}"

  # Find binary (check standardized location first, then fallbacks)
  local bin=""
  local search_paths=(
    "./bin/maschinepi"
    "./maschinepi"
    "./maschinepi_artefacts/Release/maschinepi"
    "./maschinepi_artefacts/Debug/maschinepi"
    "./maschinepi_artefacts/Release/Standalone/maschinepi"
  )

  for path in "${search_paths[@]}"; do
    if [[ -x "${path}" ]]; then
      bin="${path}"
      break
    fi
  done

  if [[ -z "${bin}" ]]; then
    log_error "Could not find maschinepi binary."
    echo "  Searched: ${search_paths[*]}"
    echo "  Have you run './mpi build' first?"
    exit 2
  fi

  log_info "Running ${bin}"

  # Check for display on Linux
  if [[ -z "${DISPLAY:-}" ]] && [[ "$(uname)" == "Linux" ]]; then
    log_warn "\$DISPLAY is empty. No GUI will appear."
  fi

  # Run with PipeWire/JACK bridge
  if command -v pw-jack >/dev/null 2>&1; then
    exec ${gdb_prefix} pw-jack "${bin}" "${@:2}"
  else
    exec ${gdb_prefix} "${bin}" "${@:2}"
  fi
}

# =================== Test Function ===================
do_test() {
  local coverage="${1:-}"
  local coverage_enabled=0

  if [[ "${coverage}" == "--coverage" ]]; then
    coverage_enabled=1
    log_info "Running tests with coverage"

    # Rebuild with coverage if needed
    cd "${ROOT_DIR}/${BUILD_DIR}"
    if [[ -f CMakeCache.txt ]] && ! grep -q "ENABLE_COVERAGE:BOOL=ON" CMakeCache.txt; then
      log_info "Rebuilding with coverage enabled..."
      cmake -DENABLE_COVERAGE=ON .
      cmake --build . -j"$(nproc)"
    fi
  else
    log_info "Running tests"
  fi

  cd "${ROOT_DIR}/${BUILD_DIR}"

  if ! command -v ctest >/dev/null 2>&1; then
    log_error "ctest not found. Please install CMake."
    exit 2
  fi

  ctest --output-on-failure -j"$(nproc)"

  if [[ "${coverage_enabled}" -eq 1 ]] && command -v gcovr >/dev/null 2>&1; then
    log_info "Generating coverage report"
    gcovr -r "${ROOT_DIR}" \
      --filter "${ROOT_DIR}/src" \
      --exclude "${ROOT_DIR}/external" \
      --exclude "${ROOT_DIR}/tests" \
      --gcov-ignore-errors=no_working_dir_found \
      --gcov-ignore-errors=source_not_found \
      --print-summary
  fi

  log_success "Tests complete!"
}

# =================== Emulator Function ===================
setup_emulator_venv() {
  local emu_dir="${ROOT_DIR}/tools/mk3-emulator"
  local venv_dir="${emu_dir}/.venv"

  # Create venv if it doesn't exist
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
    log_success "Emulator environment ready"
  fi

  # Check if deps need updating (requirements.txt newer than venv)
  if [[ "${emu_dir}/requirements.txt" -nt "${venv_dir}/pyvenv.cfg" ]]; then
    log_info "Updating emulator dependencies..."
    "${venv_dir}/bin/pip" install --quiet -r "${emu_dir}/requirements.txt"
  fi
}

do_run_emulator() {
  local emu_dir="${ROOT_DIR}/tools/mk3-emulator"
  local venv_dir="${emu_dir}/.venv"
  local skip_build=false

  for arg in "$@"; do
    if [[ "${arg}" == "--no-build" ]]; then
      skip_build=true
    fi
  done

  setup_emulator_venv

  # Launch emulator in background
  log_info "Starting MK3 emulator (TCP :9999, HTTP API :9998)"
  "${venv_dir}/bin/python3" "${emu_dir}/emulator.py" &
  local emu_pid=$!

  # Kill emulator when we exit
  trap "kill ${emu_pid} 2>/dev/null; wait ${emu_pid} 2>/dev/null" EXIT

  # Wait for emulator TCP server to be ready
  log_info "Waiting for emulator to start..."
  local retries=0
  # Use the control-plane health endpoint; opening the controller TCP socket
  # would look like a short-lived application connection to the emulator.
  while ! curl -fsS http://127.0.0.1:9998/health >/dev/null 2>&1; do
    sleep 0.2
    retries=$((retries + 1))
    if [[ ${retries} -ge 25 ]]; then
      log_error "Emulator failed to start (timeout after 5s)"
      exit 1
    fi
    # Check emulator process is still alive
    if ! kill -0 ${emu_pid} 2>/dev/null; then
      log_error "Emulator process exited unexpectedly"
      exit 1
    fi
  done
  log_success "Emulator ready"

  # Build with emulator backend (unless --no-build)
  if [[ "${skip_build}" == false ]]; then
    do_build headless --emu
  fi

  # Run the app (foreground — connects to emulator)
  # We inline run logic here instead of calling do_run() because do_run uses
  # exec, which would replace the shell and prevent our cleanup trap from firing.
  cd "${ROOT_DIR}/${BUILD_DIR}"

  local bin=""
  local search_paths=(
    "./bin/maschinepi"
    "./maschinepi"
    "./maschinepi_artefacts/Release/maschinepi"
    "./maschinepi_artefacts/Debug/maschinepi"
    "./maschinepi_artefacts/Release/Standalone/maschinepi"
  )

  for path in "${search_paths[@]}"; do
    if [[ -x "${path}" ]]; then
      bin="${path}"
      break
    fi
  done

  if [[ -z "${bin}" ]]; then
    log_error "Could not find maschinepi binary."
    echo "  Have you run './mpi build headless --emu' first?"
    exit 2
  fi

  log_info "Running ${bin} (Ctrl+C to stop both app and emulator)"
  if command -v pw-jack >/dev/null 2>&1; then
    pw-jack "${bin}"
  else
    "${bin}"
  fi
}

# =================== QA Emulator Function ===================
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

  # Launch emulator in headless mode (no display needed)
  log_info "Starting MK3 emulator (headless mode)"
  "${venv_dir}/bin/python3" "${emu_dir}/emulator.py" --headless &
  local emu_pid=$!

  # Kill emulator (and app if started) when we exit
  trap "kill ${emu_pid} 2>/dev/null; [[ -n \${app_pid:-} ]] && kill \${app_pid} 2>/dev/null; wait 2>/dev/null" EXIT

  # Wait for emulator TCP server to be ready
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

  # Optionally build
  if [[ "${do_build}" == true ]]; then
    do_build headless --emu
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

  # Find and run app in background
  cd "${ROOT_DIR}/${BUILD_DIR}"
  local bin=""
  local search_paths=(
    "./bin/maschinepi"
    "./maschinepi"
    "./maschinepi_artefacts/Release/maschinepi"
    "./maschinepi_artefacts/Debug/maschinepi"
  )
  for path in "${search_paths[@]}"; do
    if [[ -x "${path}" ]]; then
      bin="${path}"
      break
    fi
  done

  if [[ -z "${bin}" ]]; then
    log_error "Could not find maschinepi binary."
    echo "  Run './mpi build headless --emu' first, or use --build flag."
    exit 2
  fi

  log_info "Starting app in background..."
  local app_pid
  if command -v pw-jack >/dev/null 2>&1; then
    pw-jack "${bin}" &
  else
    "${bin}" &
  fi
  app_pid=$!

  # Wait for app to connect to emulator
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
  echo "    Pad:        curl -X POST localhost:9998/pad -d '{\"index\":0,\"pressure\":4000}'"
  echo ""
  echo "  Press Ctrl+C to stop both."
  wait ${app_pid} ${emu_pid}
}

# =================== Clean Function ===================
do_clean() {
  log_info "Cleaning build artifacts..."

  if [[ -d "${ROOT_DIR}/${BUILD_DIR}" ]]; then
    rm -rf "${ROOT_DIR}/${BUILD_DIR}"
    log_success "Removed ${BUILD_DIR}/"
  fi

  if [[ -d "${ROOT_DIR}/dist" ]]; then
    rm -rf "${ROOT_DIR}/dist"
    log_success "Removed dist/"
  fi

  if [[ -d "${ROOT_DIR}/.ccache" ]]; then
    read -p "Also clear ccache? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
      rm -rf "${ROOT_DIR}/.ccache"
      log_success "Removed .ccache/"
    fi
  fi

  log_success "Clean complete!"
}

# =================== Image Function (Legacy) ===================
do_image() {
  local compress="${1:-}"

  log_warn "Using legacy pi-gen approach. Consider using './mpi gen-img' instead."
  log_info "Creating Raspberry Pi image..."

  if [[ ! -f "${ROOT_DIR}/pi-tools/package-for-pi.sh" ]]; then
    log_error "pi-tools/package-for-pi.sh not found"
    exit 1
  fi

  local args=()
  if [[ "${compress}" == "--compress" ]]; then
    args+=("--compress")
  fi

  cd "${ROOT_DIR}/pi-tools"
  ./package-for-pi.sh "${args[@]}"

  log_success "Pi image creation complete!"
}

# =================== Generate Manual Package Function ===================
do_gen_manual_package() {
  local pi_build_dir="${ROOT_DIR}/${BUILD_PI_DIR}"
  local pi_tools_dir="${ROOT_DIR}/pi-tools"
  local output_dir="${pi_tools_dir}/output"
  local pkg_dir="${output_dir}/maschinepi-manual"
  local date_stamp="$(date +%Y%m%d)"

  log_info "Creating manual install package..."

  # Check for ARM64 binaries
  local maschinepi_bin=""
  for path in \
    "${pi_build_dir}/maschinepi_artefacts/Release/maschinepi" \
    "${pi_build_dir}/bin/maschinepi" \
    "${pi_build_dir}/maschinepi"; do
    if [[ -f "${path}" ]]; then
      maschinepi_bin="${path}"
      break
    fi
  done

  if [[ -z "${maschinepi_bin}" ]]; then
    log_error "maschinepi binary not found. Run './mpi build pi' first."
    exit 1
  fi

  # Check for mk3_cli
  local mk3_cli_bin=""
  for path in "${pi_build_dir}/mk3-utils/mk3_cli" "${pi_build_dir}/bin/mk3_cli"; do
    if [[ -f "${path}" ]]; then
      mk3_cli_bin="${path}"
      break
    fi
  done

  if [[ -z "${mk3_cli_bin}" ]]; then
    log_error "mk3_cli binary not found. Run './mpi build pi' first."
    exit 1
  fi

  # Verify ARM64
  if ! file "${maschinepi_bin}" | grep -q "ARM aarch64"; then
    log_error "Binary is not ARM64. Run './mpi build pi' first."
    exit 1
  fi

  # Create package directory
  mkdir -p "${output_dir}"
  rm -rf "${pkg_dir}"
  mkdir -p "${pkg_dir}/bin"
  mkdir -p "${pkg_dir}/services"
  mkdir -p "${pkg_dir}/config"

  # Copy binaries
  log_info "Copying binaries..."
  cp "${maschinepi_bin}" "${pkg_dir}/bin/"
  cp "${mk3_cli_bin}" "${pkg_dir}/bin/"

  if [[ -f "${pi_tools_dir}/maschinepi-launcher.sh" ]]; then
    cp "${pi_tools_dir}/maschinepi-launcher.sh" "${pkg_dir}/bin/"
  fi

  # Copy service file
  log_info "Copying service files..."
  if [[ -f "${pi_tools_dir}/maschinepi.service" ]]; then
    cp "${pi_tools_dir}/maschinepi.service" "${pkg_dir}/services/"
  fi

  # Create config files
  log_info "Creating config files..."

  # Sysctl config
  cat > "${pkg_dir}/config/99-realtime-audio.conf" << 'EOF'
# MaschinePI realtime audio optimizations
vm.swappiness=10
vm.dirty_ratio=3
vm.dirty_background_ratio=1
kernel.sched_rt_runtime_us=-1
EOF

  # Udev rules
  cat > "${pkg_dir}/config/99-mk3-controller.rules" << 'EOF'
# Native Instruments Maschine MK3 Controller
SUBSYSTEM=="usb", ATTRS{idVendor}=="17cc", ATTRS{idProduct}=="1600", MODE="0664", GROUP="audio"
EOF

  # WiFi template
  cat > "${pkg_dir}/config/wifi.txt" << 'EOF'
# MaschinePI WiFi Configuration
# Uncomment and fill in your WiFi details:
# WIFI_SSID="YourNetworkName"
# WIFI_PASSWORD="YourPassword"
# WIFI_COUNTRY="US"
EOF

  # Create install script
  log_info "Creating install script..."
  cat > "${pkg_dir}/install.sh" << 'INSTALL_SCRIPT'
#!/bin/bash
#
# MaschinePI Manual Install Script
# Run this on a fresh Raspberry Pi OS Lite (64-bit) installation
#
set -e

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; BLUE='\033[0;34m'; NC='\033[0m'

log_info()    { echo -e "${BLUE}==> ${NC}$*"; }
log_success() { echo -e "${GREEN}    ✓ ${NC}$*"; }
log_error()   { echo -e "${RED}==> ERROR: ${NC}$*" >&2; }

# Show message on MK3 display (if connected)
mk3_show() {
  /usr/local/bin/mk3_cli --text "$1" --target both 2>/dev/null || true
}

# Check root
[[ "$EUID" -ne 0 ]] && { log_error "Run as root: sudo ./install.sh"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "$0")"; pwd)"

echo ""
echo "=========================================="
echo "  MaschinePI Installer"
echo "=========================================="
echo ""

# Install mk3_cli dependencies first (so we can show progress on MK3 display)
log_info "Updating packages..."
apt-get update -qq

log_info "Installing mk3_cli dependencies..."
apt-get install -y -qq libusb-1.0-0 libfreetype6
log_success "mk3_cli dependencies installed"

# Install binaries (now mk3_cli will work)
log_info "Installing binaries..."
install -m 755 "${SCRIPT_DIR}/bin/"* /usr/local/bin/
log_success "Binaries installed"

mk3_show "Installing..."

# Install service & config files
log_info "Installing services and config..."
cp "${SCRIPT_DIR}/services/"*.service /etc/systemd/system/ 2>/dev/null || true
mkdir -p /etc/sysctl.d /etc/udev/rules.d /boot/firmware/maschinepi
cp "${SCRIPT_DIR}/config/99-realtime-audio.conf" /etc/sysctl.d/
cp "${SCRIPT_DIR}/config/99-mk3-controller.rules" /etc/udev/rules.d/
cp "${SCRIPT_DIR}/config/wifi.txt" /boot/firmware/maschinepi/
log_success "Config installed"

mk3_show "Installing PipeWire..."
log_info "Installing PipeWire..."
apt-get install -y -qq pipewire pipewire-alsa pipewire-jack pipewire-pulse wireplumber
log_success "PipeWire installed"

mk3_show "Installing deps..."
log_info "Installing remaining dependencies..."
apt-get install -y -qq fonts-dejavu-core libfontconfig1 ca-certificates
apt-get install -y -qq libasound2t64 2>/dev/null || apt-get install -y -qq libasound2
log_success "Dependencies installed"

# Configure system
mk3_show "Configuring..."
log_info "Configuring system..."

# Groups
getent group audio >/dev/null || groupadd audio
getent group pipewire >/dev/null || groupadd pipewire

# User
USER="${SUDO_USER:-mpi}"
id "${USER}" &>/dev/null && usermod -aG audio,pipewire,plugdev "${USER}"
log_success "User ${USER} configured"

# Autologin
mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf << EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin ${USER} --noclear %I \$TERM
EOF
log_success "Autologin configured"

# Hostname
echo "maschinepi" > /etc/hostname
sed -i 's/raspberrypi/maschinepi/g' /etc/hosts 2>/dev/null || true
hostnamectl set-hostname maschinepi 2>/dev/null || true
log_success "Hostname set to maschinepi"

# Apply settings
sysctl -p /etc/sysctl.d/99-realtime-audio.conf 2>/dev/null || true
udevadm control --reload-rules 2>/dev/null || true
udevadm trigger 2>/dev/null || true

# Enable services
log_info "Enabling services..."
systemctl daemon-reload
systemctl enable maschinepi.service 2>/dev/null || true
log_success "Services enabled"

# Cleanup
apt-get clean -qq
rm -rf /var/lib/apt/lists/*

mk3_show "Done! Reboot now"

echo ""
echo "=========================================="
echo "  MaschinePI installed successfully!"
echo "=========================================="
echo ""
echo "  Reboot to start: sudo reboot"
echo ""
echo "  After reboot:"
echo "    - SSH: ssh ${USER}@maschinepi"
echo "    - MaschinePI starts automatically"
echo ""
INSTALL_SCRIPT

  chmod +x "${pkg_dir}/install.sh"

  # Create README
  cat > "${pkg_dir}/README.txt" << 'EOF'
MaschinePI Manual Install Package
=================================

Install MaschinePI on Raspberry Pi OS Lite (64-bit).

QUICK START
-----------

1. Flash Raspberry Pi OS Lite (64-bit) to SD card
2. Boot Pi, create user, enable SSH (raspi-config)
3. Copy and install:

   scp maschinepi-manual-*.zip user@raspberrypi:~/
   ssh user@raspberrypi
   unzip maschinepi-manual-*.zip
   cd maschinepi-manual
   sudo ./install.sh
   sudo reboot

CONTENTS
--------

bin/        - maschinepi, mk3_cli (ARM64 binaries)
services/   - systemd service files
config/     - sysctl, udev, wifi config
install.sh  - Installation script

WIFI (Optional)
---------------

Edit before reboot: sudo nano /boot/firmware/maschinepi/wifi.txt

  WIFI_SSID="YourNetwork"
  WIFI_PASSWORD="YourPassword"
  WIFI_COUNTRY="US"

EOF

  # Create zip file
  log_info "Creating zip archive..."
  cd "${output_dir}"
  rm -f "maschinepi-manual-${date_stamp}.zip"
  zip -r "maschinepi-manual-${date_stamp}.zip" "maschinepi-manual"
  rm -rf "${pkg_dir}"

  local zip_file="${output_dir}/maschinepi-manual-${date_stamp}.zip"

  echo ""
  echo "=========================================="
  echo "  Manual install package created!"
  echo "=========================================="
  echo ""
  echo "  Output: ${zip_file}"
  echo "  Size:   $(du -h "${zip_file}" | cut -f1)"
  echo ""
  echo "  Quick start:"
  echo "    scp ${zip_file} user@raspberrypi:~/"
  echo "    ssh user@raspberrypi"
  echo "    unzip $(basename ${zip_file})"
  echo "    cd maschinepi-manual && sudo ./install.sh"
  echo "    sudo reboot"
  echo ""
}

# =================== Generate Image Function ===================
do_gen_img() {
  local base_image=""
  local compress=""
  local manual=""

  # Parse arguments
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --compress) compress="--compress"; shift ;;
      --manual) manual="1"; shift ;;
      -*) log_error "Unknown option: $1"; exit 1 ;;
      *) base_image="$1"; shift ;;
    esac
  done

  # Manual mode - just create a zip package
  if [[ "${manual}" == "1" ]]; then
    do_gen_manual_package
    return
  fi

  if [[ -z "${base_image}" ]]; then
    log_error "Base image path required"
    echo ""
    echo "Usage: ./mpi gen-img /path/to/raspios-lite-arm64.img [--compress]"
    echo "       ./mpi gen-img --manual   # Create manual install package"
    echo ""
    echo "Download Raspberry Pi OS Lite (64-bit) from:"
    echo "  https://www.raspberrypi.com/software/operating-systems/"
    exit 1
  fi

  if [[ ! -f "${base_image}" ]]; then
    log_error "Base image not found: ${base_image}"
    exit 1
  fi

  # Check for ARM64 binaries
  # JUCE puts the main binary in maschinepi_artefacts/Release/
  # mk3 utilities go to bin/ or mk3-utils/
  local pi_build_dir="${ROOT_DIR}/${BUILD_PI_DIR}"
  local maschinepi_bin=""
  local missing_bins=()

  # Find maschinepi binary (JUCE output location)
  for path in \
    "${pi_build_dir}/maschinepi_artefacts/Release/maschinepi" \
    "${pi_build_dir}/bin/maschinepi" \
    "${pi_build_dir}/maschinepi"; do
    if [[ -f "${path}" ]]; then
      maschinepi_bin="${path}"
      break
    fi
  done

  if [[ -z "${maschinepi_bin}" ]]; then
    missing_bins+=("maschinepi")
  fi

  # Check for mk3 utilities
  local utils_dir="${pi_build_dir}/mk3-utils"
  local mk3_bins=("mk3-boot-display" "mk3-boot-listener" "maschinepi-system-config")
  for bin in "${mk3_bins[@]}"; do
    if [[ ! -f "${utils_dir}/${bin}" ]] && [[ ! -f "${pi_build_dir}/bin/${bin}" ]]; then
      missing_bins+=("${bin}")
    fi
  done

  if [[ ${#missing_bins[@]} -gt 0 ]]; then
    log_error "Missing ARM64 binaries:"
    for bin in "${missing_bins[@]}"; do
      echo "  - ${bin}"
    done
    echo ""
    echo "Run './mpi build pi' first to cross-compile for ARM64."
    exit 1
  fi

  # Verify maschinepi is ARM64
  local first_bin="${maschinepi_bin}"
  if ! file "${first_bin}" | grep -q "ARM aarch64"; then
    log_error "Binary is not ARM64: ${first_bin}"
    echo ""
    echo "$(file "${first_bin}")"
    echo ""
    echo "Run './mpi build pi' to cross-compile for ARM64."
    exit 1
  fi

  log_info "Generating Pi image from: ${base_image}"

  # Run injection script with build directory (it will find binaries in various locations)
  local inject_args=("${base_image}" "${pi_build_dir}")
  if [[ "${compress}" == "--compress" ]]; then
    inject_args+=("--compress")
  fi

  "${ROOT_DIR}/pi-tools/inject-into-image.sh" "${inject_args[@]}"

  log_success "Image generation complete!"
}

# =================== Interactive Menu ===================
show_menu() {
  print_header

  echo -e "${CYAN}Select an option:${NC}"
  echo ""
  echo "  ${BOLD}1)${NC} Build desktop      - Build for desktop development (GUI)"
  echo "  ${BOLD}2)${NC} Build Pi           - Build for Raspberry Pi (headless)"
  echo "  ${BOLD}3)${NC} Build debug        - Build with debug symbols"
  echo "  ${BOLD}4)${NC} Run                - Run the application"
  echo "  ${BOLD}5)${NC} Test               - Run unit tests"
  echo "  ${BOLD}6)${NC} Test + coverage    - Run tests with coverage report"
  echo "  ${BOLD}7)${NC} Create Pi image    - Build bootable Raspberry Pi image"
  echo "  ${BOLD}8)${NC} Clean              - Remove build artifacts"
  echo "  ${BOLD}9)${NC} Build tools        - Build with helper tools"
  echo ""
  echo "  ${BOLD}q)${NC} Quit"
  echo ""

  read -p "Enter choice [1-9, q]: " -n 1 -r choice
  echo ""

  case "${choice}" in
    1) do_build desktop ;;
    2) do_build pi ;;
    3) do_build debug Debug ;;
    4) do_run ;;
    5) do_test ;;
    6) do_test --coverage ;;
    7) do_image ;;
    8) do_clean ;;
    9) do_build tools ;;
    q|Q) echo "Bye!"; exit 0 ;;
    *)
      log_error "Invalid choice: ${choice}"
      exit 1
      ;;
  esac
}

# =================== Usage ===================
usage() {
  print_header
  cat <<EOF
${BOLD}USAGE:${NC}
  ./mpi [command] [options]

${BOLD}COMMANDS:${NC}
  build [target] [--emu]
                    Build the project
                    Targets: headless (default), desktop, pi, tools, debug, release
                    --emu: Use socket emulator instead of USB mk3 hardware
                    For pi: cross-compiles for ARM64

  build pi --check  Check if ARM64 cross-compiler is installed
  build pi --install-deps
                    Install all ARM64 cross-compilation dependencies

  run [--gdb]       Run the application
                    --gdb: Run with GDB debugger

  test [--coverage] Run unit tests
                    --coverage: Generate coverage report

  gen-img <base.img> [--compress]
                    Generate Pi image by injecting binaries into base image
                    Requires: ./mpi build pi first

  gen-img --manual  Create manual install package (zip)
                    Copy to Pi and run install.sh

  flash-sd [image]  Write Pi OS image to SD card with prompts for:
                    - Username/password
                    - WiFi (optional)
                    - Enables SSH automatically

  image [--compress] (Legacy) Create Pi image via pi-gen
                    --compress: Compress the final image

  run-emulator [--no-build]
                    Start emulator + build + run app (all-in-one)
                    --no-build: skip build, just start emulator and run
                    Aliases: emulator, emu

  qa-emulator [--build] [--no-app]
                    Start headless emulator + app for automated QA testing
                    --build: rebuild with emulator backend first
                    --no-app: start only the emulator (no app)
                    Aliases: qa-emu

  clean             Remove build artifacts

${BOLD}EXAMPLES:${NC}
  ./mpi                       # Interactive menu
  ./mpi build                 # Build headless (default)
  ./mpi build headless        # Build headless explicitly
  ./mpi build headless --emu  # Build with socket emulator backend
  ./mpi build desktop         # Build desktop (GUI enabled)
  ./mpi build pi              # Cross-compile for Raspberry Pi
  ./mpi build pi --check      # Check for ARM64 toolchain
  ./mpi build pi --install-deps # Install ARM64 dependencies
  ./mpi build debug           # Build with debug symbols
  ./mpi run                   # Run the application
  ./mpi run --gdb             # Run with debugger
  ./mpi run-emulator          # Start MK3 emulator (auto-creates venv)
  ./mpi qa-emulator --build   # Headless emulator + app for automated QA
  ./mpi test                  # Run tests
  ./mpi test --coverage       # Run tests with coverage
  ./mpi gen-img ~/raspios.img # Generate Pi image
  ./mpi gen-img --manual      # Create manual install package
  ./mpi flash-sd              # Write image to SD card (interactive)
  ./mpi clean                 # Clean all build artifacts

${BOLD}PI SETUP (Recommended):${NC}
  1. Build for Pi:
     ./mpi build pi --install-deps   # First time only
     ./mpi build pi

  2. Flash SD card (interactive prompts for user/wifi):
     ./mpi flash-sd ~/Downloads/raspios-lite-arm64.img.xz

  3. Create install package:
     ./mpi gen-img --manual

  4. Boot Pi, SSH in, install:
     scp pi-tools/output/maschinepi-manual-*.zip user@maschinepi:~/
     ssh user@maschinepi
     unzip maschinepi-manual-*.zip
     cd maschinepi-manual && sudo ./install.sh
     sudo reboot

${BOLD}ENVIRONMENT VARIABLES:${NC}
  BUILD_DIR         Build directory (default: build)
  BUILD_PI_DIR      Pi build directory (default: build-pi)
  CCACHE_DIR        ccache directory (default: .ccache)
  CMAKE_GENERATOR   CMake generator (auto-detected)

EOF
}

# =================== Flash SD Card Function ===================
do_flash_sd() {
  local image_path="${1:-}"
  local mount_point=""

  # Cleanup function for safe exit
  flash_cleanup() {
    if [[ -n "${mount_point}" ]] && mountpoint -q "${mount_point}" 2>/dev/null; then
      sudo umount "${mount_point}" 2>/dev/null || true
    fi
    [[ -n "${mount_point}" ]] && rmdir "${mount_point}" 2>/dev/null || true
  }
  trap flash_cleanup EXIT

  echo ""
  echo "=========================================="
  echo "  MaschinePI SD Card Writer"
  echo "=========================================="
  echo ""

  # Check for required tools
  for tool in dd lsblk openssl sync partprobe; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
      log_error "Required tool not found: ${tool}"
      exit 1
    fi
  done

  # Get image path if not provided
  if [[ -z "${image_path}" ]]; then
    echo "Enter path to Raspberry Pi OS Lite image:"
    echo "(e.g., ~/Downloads/2024-raspios-lite-arm64.img.xz)"
    read -rp "> " image_path
  fi

  # Expand tilde
  image_path="${image_path/#\~/$HOME}"

  if [[ ! -f "${image_path}" ]]; then
    log_error "Image not found: ${image_path}"
    exit 1
  fi

  # List available block devices (exclude system disk)
  echo ""
  echo "Available removable devices:"
  echo ""
  lsblk -d -o NAME,SIZE,MODEL,TRAN,RM | grep -E "usb|mmc|1$" | grep -v "NAME" || \
    lsblk -d -o NAME,SIZE,MODEL | grep -v "loop\|sr\|nvme\|sda\|NAME"
  echo ""

  echo "Enter SD card device (e.g., sdb, mmcblk0):"
  echo "WARNING: All data on this device will be PERMANENTLY ERASED!"
  read -rp "> " sd_device

  # Normalize device path
  if [[ ! "${sd_device}" =~ ^/dev/ ]]; then
    sd_device="/dev/${sd_device}"
  fi

  # Remove trailing partition number if user entered it
  # mmcblk devices use pattern: mmcblk0 (device), mmcblk0p1 (partition)
  # sd devices use pattern: sdb (device), sdb1 (partition)
  if [[ "${sd_device}" =~ mmcblk ]]; then
    # For mmcblk: strip pN suffix (e.g., mmcblk0p1 -> mmcblk0)
    sd_device="${sd_device%%p[0-9]*}"
  else
    # For sd/nvme: strip trailing digit(s) (e.g., sdb1 -> sdb)
    sd_device="${sd_device%%[0-9]*}"
  fi

  if [[ ! -b "${sd_device}" ]]; then
    log_error "Not a block device: ${sd_device}"
    exit 1
  fi

  # SAFETY: Prevent writing to system disk
  local root_device=$(df / | tail -1 | awk '{print $1}' | sed 's/[0-9]*$//' | sed 's/p$//')
  if [[ "${sd_device}" == "${root_device}" ]]; then
    log_error "REFUSING to write to system disk: ${sd_device}"
    exit 1
  fi

  # SAFETY: Check if device contains important filesystems
  if lsblk -no MOUNTPOINT "${sd_device}" 2>/dev/null | grep -qE "^/$|^/home|^/boot|^/var"; then
    log_error "Device ${sd_device} contains system partitions. Aborting."
    exit 1
  fi

  # Unmount all partitions on the device
  log_info "Unmounting any mounted partitions on ${sd_device}..."
  for part in "${sd_device}"* "${sd_device}"p*; do
    if [[ -b "${part}" ]] && mountpoint -q "$(lsblk -no MOUNTPOINT "${part}" 2>/dev/null)" 2>/dev/null; then
      sudo umount "${part}" 2>/dev/null || true
    fi
  done
  # Also use umount with device directly
  sudo umount "${sd_device}"?* 2>/dev/null || true
  sudo umount "${sd_device}"p?* 2>/dev/null || true
  sleep 1

  # Get username
  echo ""
  echo "Enter username for Pi (default: mpi):"
  read -rp "> " pi_user
  pi_user="${pi_user:-mpi}"

  # Validate username
  if [[ ! "${pi_user}" =~ ^[a-z_][a-z0-9_-]*$ ]]; then
    log_error "Invalid username. Use lowercase letters, numbers, underscore, hyphen."
    exit 1
  fi

  # Get password
  echo ""
  echo "Enter password for ${pi_user}:"
  read -rs pi_password
  echo ""

  if [[ -z "${pi_password}" ]]; then
    log_error "Password cannot be empty"
    exit 1
  fi

  # Confirm password
  echo "Confirm password:"
  read -rs pi_password_confirm
  echo ""

  if [[ "${pi_password}" != "${pi_password_confirm}" ]]; then
    log_error "Passwords do not match"
    exit 1
  fi

  # Get WiFi settings (optional)
  echo ""
  echo "Configure WiFi? (leave empty to skip)"
  read -rp "WiFi SSID: " wifi_ssid

  wifi_password=""
  wifi_country="US"
  if [[ -n "${wifi_ssid}" ]]; then
    read -rs -p "WiFi Password: " wifi_password
    echo ""
    if [[ -z "${wifi_password}" ]]; then
      log_error "WiFi password cannot be empty"
      exit 1
    fi
    read -rp "Country code (default: US): " wifi_country
    wifi_country="${wifi_country:-US}"
    wifi_country="${wifi_country^^}"  # Uppercase
  fi

  # Show device info
  echo ""
  echo "=========================================="
  echo "  Target Device Info"
  echo "=========================================="
  lsblk -o NAME,SIZE,MODEL,SERIAL "${sd_device}" 2>/dev/null || lsblk "${sd_device}"
  echo ""

  # Confirm
  echo "=========================================="
  echo "  Configuration Summary"
  echo "=========================================="
  echo ""
  echo "  Image:    $(basename "${image_path}")"
  echo "  Device:   ${sd_device}"
  echo "  Username: ${pi_user}"
  echo "  Password: ********"
  if [[ -n "${wifi_ssid}" ]]; then
    echo "  WiFi:     ${wifi_ssid} (${wifi_country})"
  else
    echo "  WiFi:     Not configured (use ethernet)"
  fi
  echo ""
  echo "${RED}WARNING: This will PERMANENTLY ERASE ALL DATA on ${sd_device}!${NC}"
  echo ""
  read -rp "Type 'yes' to continue: " confirm

  if [[ "${confirm}" != "yes" ]]; then
    echo "Aborted."
    exit 0
  fi

  # Final unmount check
  sudo umount "${sd_device}"?* 2>/dev/null || true
  sudo umount "${sd_device}"p?* 2>/dev/null || true
  sync

  # Write image to SD card
  log_info "Writing image to ${sd_device}..."
  log_info "This will take several minutes. Do not remove the SD card."
  echo ""

  # Use pipefail to catch decompression errors
  set -o pipefail

  local dd_cmd="sudo dd of=${sd_device} bs=4M status=progress conv=fdatasync oflag=direct"

  if [[ "${image_path}" == *.xz ]]; then
    xz -dc "${image_path}" | ${dd_cmd}
  elif [[ "${image_path}" == *.gz ]]; then
    gunzip -c "${image_path}" | ${dd_cmd}
  elif [[ "${image_path}" == *.zip ]]; then
    unzip -p "${image_path}" "*.img" | ${dd_cmd}
  else
    sudo dd if="${image_path}" of="${sd_device}" bs=4M status=progress conv=fdatasync oflag=direct
  fi

  local dd_status=$?
  set +o pipefail

  if [[ ${dd_status} -ne 0 ]]; then
    log_error "Failed to write image (exit code: ${dd_status})"
    exit 1
  fi

  # Sync all buffers to disk
  log_info "Syncing buffers to disk..."
  sync
  sudo sync
  sleep 1
  sync

  log_success "Image written successfully"

  # Re-read partition table with retries
  log_info "Reading partition table..."
  sudo partprobe "${sd_device}" 2>/dev/null || true
  sleep 2
  sudo partprobe "${sd_device}" 2>/dev/null || true

  # Wait for partitions to appear (up to 10 seconds)
  local boot_part=""
  for i in {1..10}; do
    if [[ -b "${sd_device}1" ]]; then
      boot_part="${sd_device}1"
      break
    elif [[ -b "${sd_device}p1" ]]; then
      boot_part="${sd_device}p1"
      break
    fi
    sleep 1
  done

  if [[ -z "${boot_part}" ]]; then
    log_error "Could not find boot partition after 10 seconds"
    log_error "Partitions found:"
    ls -la "${sd_device}"* 2>/dev/null || true
    exit 1
  fi

  # Determine root partition
  local root_part=""
  if [[ -b "${sd_device}2" ]]; then
    root_part="${sd_device}2"
  elif [[ -b "${sd_device}p2" ]]; then
    root_part="${sd_device}p2"
  fi

  log_info "Found partitions: boot=${boot_part}, root=${root_part:-none}"

  # Create mount points
  local boot_mount=$(mktemp -d)
  local root_mount=$(mktemp -d)

  # Update cleanup trap
  flash_cleanup() {
    mountpoint -q "${boot_mount}" 2>/dev/null && sudo umount "${boot_mount}" 2>/dev/null
    mountpoint -q "${root_mount}" 2>/dev/null && sudo umount "${root_mount}" 2>/dev/null
    rmdir "${boot_mount}" "${root_mount}" 2>/dev/null || true
  }
  trap flash_cleanup EXIT

  # Wait for filesystems to be ready
  sleep 2

  # Mount boot partition
  log_info "Mounting boot partition..."
  if ! sudo mount "${boot_part}" "${boot_mount}"; then
    log_error "Failed to mount boot partition"
    exit 1
  fi

  # Mount root partition (needed for NetworkManager config on Bookworm/Trixie)
  if [[ -n "${root_part}" ]]; then
    log_info "Mounting root partition..."
    if ! sudo mount "${root_part}" "${root_mount}"; then
      log_warn "Failed to mount root partition - WiFi may need manual config"
      root_part=""
    fi
  fi

  # ========== BOOT PARTITION CONFIG ==========

  # Enable SSH (empty file in boot partition)
  log_info "Enabling SSH..."
  sudo touch "${boot_mount}/ssh"

  # Create user config (Pi OS Bullseye+)
  log_info "Configuring user ${pi_user}..."
  local encrypted_pw=$(echo "${pi_password}" | openssl passwd -6 -stdin)
  echo "${pi_user}:${encrypted_pw}" | sudo tee "${boot_mount}/userconf.txt" > /dev/null

  # Configure WiFi if provided
  if [[ -n "${wifi_ssid}" ]]; then
    log_info "Configuring WiFi for ${wifi_ssid}..."

    # ==== Method 1: custom.toml (Bookworm official method) ====
    # This is read by rpi-first-boot-wizard on first boot
    cat << CUSTOMTOML | sudo tee "${boot_mount}/custom.toml" > /dev/null
# Raspberry Pi OS first-boot configuration
# Generated by MaschinePI flash-sd

[system]
hostname = "maschinepi"

[user]
name = "${pi_user}"
password_encrypted = "${encrypted_pw}"

[wifi]
ssid = "${wifi_ssid}"
password = "${wifi_password}"
country = "${wifi_country}"
hidden = false

[locale]
keymap = "us"
timezone = "UTC"
CUSTOMTOML
    log_success "Created custom.toml for first-boot wizard"

    # ==== Method 2: NetworkManager connection file (Bookworm/Trixie) ====
    # This is the direct method that works if first-boot wizard fails
    if [[ -n "${root_part}" ]] && mountpoint -q "${root_mount}"; then
      local nm_dir="${root_mount}/etc/NetworkManager/system-connections"
      sudo mkdir -p "${nm_dir}"

      # Generate a UUID for the connection
      local conn_uuid=$(cat /proc/sys/kernel/random/uuid 2>/dev/null || uuidgen 2>/dev/null || echo "$(date +%s)-wifi")

      # Create NetworkManager connection file
      # Note: PSK must be the raw password for NetworkManager to hash it
      cat << NMCONN | sudo tee "${nm_dir}/${wifi_ssid}.nmconnection" > /dev/null
[connection]
id=${wifi_ssid}
uuid=${conn_uuid}
type=wifi
interface-name=wlan0
autoconnect=true

[wifi]
mode=infrastructure
ssid=${wifi_ssid}

[wifi-security]
auth-alg=open
key-mgmt=wpa-psk
psk=${wifi_password}

[ipv4]
method=auto

[ipv6]
method=auto
NMCONN

      # CRITICAL: Set correct permissions (NetworkManager requires 600)
      sudo chmod 600 "${nm_dir}/${wifi_ssid}.nmconnection"
      sudo chown root:root "${nm_dir}/${wifi_ssid}.nmconnection"
      log_success "Created NetworkManager connection file"

      # Set WiFi regulatory domain
      local reg_file="${root_mount}/etc/default/crda"
      if [[ -f "${reg_file}" ]]; then
        sudo sed -i "s/^REGDOMAIN=.*/REGDOMAIN=${wifi_country}/" "${reg_file}"
      fi
    fi

    # ==== Method 3: wpa_supplicant.conf (Legacy - Bullseye and older) ====
    cat << WPACFG | sudo tee "${boot_mount}/wpa_supplicant.conf" > /dev/null
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
update_config=1
country=${wifi_country}

network={
    ssid="${wifi_ssid}"
    psk="${wifi_password}"
    key_mgmt=WPA-PSK
}
WPACFG
    log_success "Created wpa_supplicant.conf (legacy fallback)"
  fi

  # ========== ROOT PARTITION CONFIG (if mounted) ==========

  if [[ -n "${root_part}" ]] && mountpoint -q "${root_mount}"; then
    # Set hostname in rootfs
    log_info "Setting hostname to maschinepi..."
    echo "maschinepi" | sudo tee "${root_mount}/etc/hostname" > /dev/null

    # Update /etc/hosts
    if [[ -f "${root_mount}/etc/hosts" ]]; then
      sudo sed -i 's/raspberrypi/maschinepi/g' "${root_mount}/etc/hosts"
    fi
    log_success "Hostname configured"
  fi

  # ========== SYNC AND UNMOUNT ==========

  log_info "Syncing filesystems..."
  sync
  sudo sync
  sleep 1

  # Unmount root first
  if mountpoint -q "${root_mount}" 2>/dev/null; then
    log_info "Unmounting root partition..."
    sudo umount "${root_mount}" || sudo umount -l "${root_mount}" || true
  fi
  rmdir "${root_mount}" 2>/dev/null || true

  # Unmount boot
  log_info "Unmounting boot partition..."
  if ! sudo umount "${boot_mount}"; then
    log_warn "Normal unmount failed, trying lazy unmount..."
    sudo umount -l "${boot_mount}" || true
  fi
  rmdir "${boot_mount}" 2>/dev/null || true

  # Clear trap
  trap - EXIT

  # Final sync
  log_info "Final sync..."
  sync
  sudo sync
  sleep 2
  sync

  # Eject if possible
  if command -v eject >/dev/null 2>&1; then
    sudo eject "${sd_device}" 2>/dev/null || true
  fi

  echo ""
  echo "=========================================="
  echo "  ${GREEN}SD Card Ready!${NC}"
  echo "=========================================="
  echo ""
  echo "  The SD card is now safe to remove."
  echo ""
  echo "  Configuration applied:"
  echo "    - SSH: enabled"
  echo "    - User: ${pi_user}"
  echo "    - Hostname: maschinepi"
  if [[ -n "${wifi_ssid}" ]]; then
    echo "    - WiFi: ${wifi_ssid}"
    echo "      (configured via custom.toml + NetworkManager)"
  fi
  echo ""
  echo "  Next steps:"
  echo "    1. Insert SD card into Raspberry Pi"
  if [[ -n "${wifi_ssid}" ]]; then
    echo "    2. Power on - WiFi should connect automatically"
  else
    echo "    2. Connect ethernet cable, then power on"
  fi
  echo "    3. Wait 2-3 minutes for first boot to complete"
  echo "    4. Find Pi:"
  echo "         ping maschinepi.local"
  echo "         # or check your router's DHCP list"
  echo "    5. SSH in:"
  echo "         ssh ${pi_user}@maschinepi.local"
  echo ""
  echo "  If WiFi doesn't connect, SSH via ethernet and run:"
  echo "    sudo nmcli dev wifi connect \"${wifi_ssid:-SSID}\" password \"PASSWORD\""
  echo ""
}

# =================== Main ===================
main() {
  cd "${ROOT_DIR}"

  # No arguments - show interactive menu
  if [[ $# -eq 0 ]]; then
    show_menu
    exit 0
  fi

  local cmd="${1}"
  shift

  case "${cmd}" in
    build)
      do_build "$@"
      ;;
    run)
      do_run "$@"
      ;;
    test)
      do_test "$@"
      ;;
    gen-img)
      do_gen_img "$@"
      ;;
    flash-sd)
      do_flash_sd "$@"
      ;;
    image)
      do_image "$@"
      ;;
    run-emulator|emulator|emu)
      do_run_emulator "$@"
      ;;
    qa-emulator|qa-emu)
      do_qa_emulator "$@"
      ;;
    clean)
      do_clean
      ;;
    -h|--help|help)
      usage
      ;;
    -v|--version)
      echo "mpi v${VERSION}"
      ;;
    *)
      log_error "Unknown command: ${cmd}"
      echo "Run './mpi --help' for usage."
      exit 1
      ;;
  esac
}

main "$@"
