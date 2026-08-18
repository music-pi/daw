#!/usr/bin/env bash
#
# Generate Pi image or manual install package
#
# Usage:
#   scripts/gen-img.sh <base.img> [--compress]
#   scripts/gen-img.sh --manual
#
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.."; pwd)"

readonly BUILD_DIR="${BUILD_DIR:-build}"
readonly BUILD_PI_DIR="${BUILD_PI_DIR:-build-pi}"

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

# =================== Generate Manual Package ===================
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

  cat > "${pkg_dir}/config/99-realtime-audio.conf" << 'EOF'
# MaschinePI realtime audio optimizations
vm.swappiness=10
vm.dirty_ratio=3
vm.dirty_background_ratio=1
kernel.sched_rt_runtime_us=-1
EOF

  cat > "${pkg_dir}/config/99-mk3-controller.rules" << 'EOF'
# Native Instruments Maschine MK3 Controller
SUBSYSTEM=="usb", ATTRS{idVendor}=="17cc", ATTRS{idProduct}=="1600", MODE="0664", GROUP="audio"
EOF

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
set -e

RED='\033[0;31m'; GREEN='\033[0;32m'; BLUE='\033[0;34m'; NC='\033[0m'
log_info()    { echo -e "${BLUE}==> ${NC}$*"; }
log_success() { echo -e "${GREEN}    ✓ ${NC}$*"; }
log_error()   { echo -e "${RED}==> ERROR: ${NC}$*" >&2; }

mk3_show() {
  /usr/local/bin/mk3_cli --text "$1" --target both 2>/dev/null || true
}

[[ "$EUID" -ne 0 ]] && { log_error "Run as root: sudo ./install.sh"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "$0")"; pwd)"

echo ""
echo "=========================================="
echo "  MaschinePI Installer"
echo "=========================================="
echo ""

log_info "Updating packages..."
apt-get update -qq

log_info "Installing mk3_cli dependencies..."
apt-get install -y -qq libusb-1.0-0 libfreetype6
log_success "mk3_cli dependencies installed"

log_info "Installing binaries..."
install -m 755 "${SCRIPT_DIR}/bin/"* /usr/local/bin/
log_success "Binaries installed"

mk3_show "Installing..."

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

mk3_show "Configuring..."
log_info "Configuring system..."

getent group audio >/dev/null || groupadd audio
getent group pipewire >/dev/null || groupadd pipewire

USER="${SUDO_USER:-mpi}"
id "${USER}" &>/dev/null && usermod -aG audio,pipewire,plugdev "${USER}"
log_success "User ${USER} configured"

mkdir -p /etc/systemd/system/getty@tty1.service.d
cat > /etc/systemd/system/getty@tty1.service.d/autologin.conf << EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin ${USER} --noclear %I \$TERM
EOF
log_success "Autologin configured"

echo "maschinepi" > /etc/hostname
sed -i 's/raspberrypi/maschinepi/g' /etc/hosts 2>/dev/null || true
hostnamectl set-hostname maschinepi 2>/dev/null || true
log_success "Hostname set to maschinepi"

sysctl -p /etc/sysctl.d/99-realtime-audio.conf 2>/dev/null || true
udevadm control --reload-rules 2>/dev/null || true
udevadm trigger 2>/dev/null || true

log_info "Enabling services..."
systemctl daemon-reload
systemctl enable maschinepi.service 2>/dev/null || true
log_success "Services enabled"

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
EOF

  # Create zip
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
  echo "    unzip $(basename "${zip_file}")"
  echo "    cd maschinepi-manual && sudo ./install.sh"
  echo "    sudo reboot"
  echo ""
}

# =================== Generate Image ===================
do_gen_img() {
  local base_image=""
  local compress=""
  local manual=""

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --compress) compress="--compress"; shift ;;
      --manual) manual="1"; shift ;;
      -*) log_error "Unknown option: $1"; exit 1 ;;
      *) base_image="$1"; shift ;;
    esac
  done

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

  local pi_build_dir="${ROOT_DIR}/${BUILD_PI_DIR}"
  local maschinepi_bin=""
  local missing_bins=()

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

  local inject_args=("${base_image}" "${pi_build_dir}")
  if [[ "${compress}" == "--compress" ]]; then
    inject_args+=("--compress")
  fi

  "${ROOT_DIR}/pi-tools/inject-into-image.sh" "${inject_args[@]}"

  log_success "Image generation complete!"
}

do_gen_img "$@"
