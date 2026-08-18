#!/usr/bin/env bash
#
# inject-into-image.sh - Inject MaschinePI binaries into a Raspberry Pi OS image
#
# Usage: ./inject-into-image.sh <base_image> <bin_dir> [--compress]
#
set -euo pipefail

# =================== Config ===================
SCRIPT_DIR="$(cd "$(dirname "$0")"; pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.."; pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/output"

# =================== Colors ===================
if [[ -t 1 ]]; then
  RED='\033[0;31m'
  GREEN='\033[0;32m'
  YELLOW='\033[1;33m'
  BLUE='\033[0;34m'
  NC='\033[0m'
else
  RED='' GREEN='' YELLOW='' BLUE='' NC=''
fi

log_info()    { echo -e "${BLUE}==> ${NC}$*"; }
log_success() { echo -e "${GREEN}==> ${NC}$*"; }
log_warn()    { echo -e "${YELLOW}==> WARNING: ${NC}$*"; }
log_error()   { echo -e "${RED}==> ERROR: ${NC}$*" >&2; }

# =================== Cleanup ===================
LOOP_DEVICE=""
MOUNT_BOOT=""
MOUNT_ROOT=""
WORK_IMG=""

cleanup() {
  log_info "Cleaning up..."

  # Unmount partitions
  if [[ -n "${MOUNT_ROOT}" ]] && mountpoint -q "${MOUNT_ROOT}" 2>/dev/null; then
    sudo umount "${MOUNT_ROOT}" || true
  fi
  if [[ -n "${MOUNT_BOOT}" ]] && mountpoint -q "${MOUNT_BOOT}" 2>/dev/null; then
    sudo umount "${MOUNT_BOOT}" || true
  fi

  # Detach loop device
  if [[ -n "${LOOP_DEVICE}" ]]; then
    sudo losetup -d "${LOOP_DEVICE}" 2>/dev/null || true
  fi

  # Remove mount directories
  [[ -n "${MOUNT_BOOT}" ]] && rm -rf "${MOUNT_BOOT}" 2>/dev/null || true
  [[ -n "${MOUNT_ROOT}" ]] && rm -rf "${MOUNT_ROOT}" 2>/dev/null || true
}

trap cleanup EXIT

# =================== Parse args ===================
BASE_IMAGE="${1:-}"
BIN_DIR="${2:-}"
COMPRESS=0

shift 2 || true
while [[ $# -gt 0 ]]; do
  case "$1" in
    --compress) COMPRESS=1; shift ;;
    *) log_error "Unknown option: $1"; exit 1 ;;
  esac
done

if [[ -z "${BASE_IMAGE}" ]] || [[ -z "${BIN_DIR}" ]]; then
  echo "Usage: $0 <base_image> <bin_dir> [--compress]"
  exit 1
fi

if [[ ! -f "${BASE_IMAGE}" ]]; then
  log_error "Base image not found: ${BASE_IMAGE}"
  exit 1
fi

if [[ ! -d "${BIN_DIR}" ]]; then
  log_error "Binary directory not found: ${BIN_DIR}"
  exit 1
fi

# Check for required tools
for tool in losetup partprobe; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    log_error "Required tool not found: ${tool}"
    exit 1
  fi
done

# Check for sudo
if [[ "$EUID" -ne 0 ]]; then
  if ! sudo -v 2>/dev/null; then
    log_error "This script requires sudo access to mount the image"
    exit 1
  fi
fi

# =================== Create working copy ===================
mkdir -p "${OUTPUT_DIR}"
DATE_STAMP="$(date +%Y%m%d)"
WORK_IMG="${OUTPUT_DIR}/maschinepi-${DATE_STAMP}.img"

log_info "Creating working copy of base image..."

# Handle compressed images
if [[ "${BASE_IMAGE}" == *.xz ]]; then
  log_info "Decompressing xz image..."
  xz -dk -c "${BASE_IMAGE}" > "${WORK_IMG}"
elif [[ "${BASE_IMAGE}" == *.gz ]]; then
  log_info "Decompressing gz image..."
  gunzip -c "${BASE_IMAGE}" > "${WORK_IMG}"
elif [[ "${BASE_IMAGE}" == *.zip ]]; then
  log_info "Extracting zip image..."
  unzip -p "${BASE_IMAGE}" "*.img" > "${WORK_IMG}"
else
  cp "${BASE_IMAGE}" "${WORK_IMG}"
fi

log_success "Working image: ${WORK_IMG}"

# =================== Mount image ===================
log_info "Setting up loop device..."

# Attach loop device with partition scanning
LOOP_DEVICE=$(sudo losetup --find --show --partscan "${WORK_IMG}")
log_info "Loop device: ${LOOP_DEVICE}"

# Wait for partitions to appear
sleep 1
sudo partprobe "${LOOP_DEVICE}" 2>/dev/null || true
sleep 1

# Find partition devices (p1 = boot, p2 = rootfs)
BOOT_PART="${LOOP_DEVICE}p1"
ROOT_PART="${LOOP_DEVICE}p2"

if [[ ! -b "${ROOT_PART}" ]]; then
  log_error "Could not find rootfs partition: ${ROOT_PART}"
  log_info "Available partitions:"
  ls -la "${LOOP_DEVICE}"* || true
  exit 1
fi

# Create mount points
MOUNT_BOOT=$(mktemp -d)
MOUNT_ROOT=$(mktemp -d)

log_info "Mounting partitions..."
sudo mount "${ROOT_PART}" "${MOUNT_ROOT}"

if [[ -b "${BOOT_PART}" ]]; then
  sudo mount "${BOOT_PART}" "${MOUNT_BOOT}"
else
  log_warn "Boot partition not found (may be combined with rootfs)"
fi

log_success "Image mounted at ${MOUNT_ROOT}"

# =================== Inject binaries ===================
log_info "Injecting binaries..."

# Find and install maschinepi (JUCE puts it in artefacts directory)
MASCHINEPI_BIN=""
for path in \
  "${BIN_DIR}/maschinepi_artefacts/Release/maschinepi" \
  "${BIN_DIR}/bin/maschinepi" \
  "${BIN_DIR}/maschinepi"; do
  if [[ -f "${path}" ]]; then
    MASCHINEPI_BIN="${path}"
    break
  fi
done

if [[ -n "${MASCHINEPI_BIN}" ]]; then
  sudo install -D -m 755 "${MASCHINEPI_BIN}" "${MOUNT_ROOT}/usr/local/bin/maschinepi"
  log_success "  Installed maschinepi"
else
  log_warn "  Binary not found: maschinepi"
fi

# Find and install mk3 utilities (in mk3-utils/ or bin/)
for bin in mk3-boot-display mk3-boot-listener maschinepi-system-config; do
  BIN_PATH=""
  for path in \
    "${BIN_DIR}/mk3-utils/${bin}" \
    "${BIN_DIR}/bin/${bin}" \
    "${BIN_DIR}/${bin}"; do
    if [[ -f "${path}" ]]; then
      BIN_PATH="${path}"
      break
    fi
  done

  if [[ -n "${BIN_PATH}" ]]; then
    sudo install -D -m 755 "${BIN_PATH}" "${MOUNT_ROOT}/usr/local/bin/${bin}"
    log_success "  Installed ${bin}"
  else
    log_warn "  Binary not found: ${bin}"
  fi
done

# Launcher script
if [[ -f "${SCRIPT_DIR}/maschinepi-launcher.sh" ]]; then
  sudo install -D -m 755 "${SCRIPT_DIR}/maschinepi-launcher.sh" "${MOUNT_ROOT}/usr/local/bin/maschinepi-launcher.sh"
  log_success "  Installed maschinepi-launcher.sh"
fi

# =================== Inject service files ===================
log_info "Installing systemd services..."

sudo mkdir -p "${MOUNT_ROOT}/etc/systemd/system"

for service in maschinepi.service mk3-boot-display.service mk3-boot-listener.service; do
  if [[ -f "${SCRIPT_DIR}/${service}" ]]; then
    sudo cp "${SCRIPT_DIR}/${service}" "${MOUNT_ROOT}/etc/systemd/system/"
    log_success "  Installed ${service}"
  fi
done

# First-boot service
if [[ -f "${SCRIPT_DIR}/first-boot.service" ]]; then
  sudo cp "${SCRIPT_DIR}/first-boot.service" "${MOUNT_ROOT}/etc/systemd/system/maschinepi-first-boot.service"
  log_success "  Installed maschinepi-first-boot.service"
fi

# =================== Inject config files ===================
log_info "Installing configuration files..."

# Realtime audio sysctl
sudo mkdir -p "${MOUNT_ROOT}/etc/sysctl.d"
cat << 'EOF' | sudo tee "${MOUNT_ROOT}/etc/sysctl.d/99-realtime-audio.conf" > /dev/null
# MaschinePI realtime audio optimizations
vm.swappiness=10
vm.dirty_ratio=3
vm.dirty_background_ratio=1
kernel.sched_rt_runtime_us=-1
EOF
log_success "  Installed 99-realtime-audio.conf"

# MK3 udev rules
sudo mkdir -p "${MOUNT_ROOT}/etc/udev/rules.d"
cat << 'EOF' | sudo tee "${MOUNT_ROOT}/etc/udev/rules.d/99-mk3-controller.rules" > /dev/null
# Native Instruments Maschine MK3 Controller
SUBSYSTEM=="usb", ATTRS{idVendor}=="17cc", ATTRS{idProduct}=="1600", MODE="0664", GROUP="audio"
EOF
log_success "  Installed 99-mk3-controller.rules"

# =================== Inject first-boot script ===================
log_info "Installing first-boot script..."

sudo mkdir -p "${MOUNT_ROOT}/opt/maschinepi"
if [[ -f "${SCRIPT_DIR}/first-boot.sh" ]]; then
  sudo install -D -m 755 "${SCRIPT_DIR}/first-boot.sh" "${MOUNT_ROOT}/opt/maschinepi/first-boot.sh"
  log_success "  Installed first-boot.sh"
fi

# =================== Enable services ===================
log_info "Enabling services..."

# Create symlinks to enable services at boot
sudo mkdir -p "${MOUNT_ROOT}/etc/systemd/system/multi-user.target.wants"
sudo mkdir -p "${MOUNT_ROOT}/etc/systemd/system/sysinit.target.wants"

for service in maschinepi.service mk3-boot-listener.service maschinepi-first-boot.service; do
  if [[ -f "${MOUNT_ROOT}/etc/systemd/system/${service}" ]]; then
    sudo ln -sf "/etc/systemd/system/${service}" \
      "${MOUNT_ROOT}/etc/systemd/system/multi-user.target.wants/${service}"
    log_success "  Enabled ${service}"
  fi
done

# mk3-boot-display needs sysinit.target for early boot
if [[ -f "${MOUNT_ROOT}/etc/systemd/system/mk3-boot-display.service" ]]; then
  sudo ln -sf "/etc/systemd/system/mk3-boot-display.service" \
    "${MOUNT_ROOT}/etc/systemd/system/sysinit.target.wants/mk3-boot-display.service"
  log_success "  Enabled mk3-boot-display.service (early boot)"
fi

# =================== Configure default user ===================
log_info "Configuring default user 'mpi'..."

# Generate encrypted password for "myMP!"
# Using SHA-512 encryption (method 6)
ENCRYPTED_PW=$(openssl passwd -6 "myMP!")

# Create userconf.txt in boot partition for Pi OS first-boot user creation
if mountpoint -q "${MOUNT_BOOT}" 2>/dev/null; then
  echo "mpi:${ENCRYPTED_PW}" | sudo tee "${MOUNT_BOOT}/userconf.txt" > /dev/null
  log_success "  Created userconf.txt in boot partition"
else
  # Try /boot/firmware if boot partition is part of rootfs
  if [[ -d "${MOUNT_ROOT}/boot/firmware" ]]; then
    echo "mpi:${ENCRYPTED_PW}" | sudo tee "${MOUNT_ROOT}/boot/firmware/userconf.txt" > /dev/null
    log_success "  Created userconf.txt in /boot/firmware"
  fi
fi

# =================== Enable SSH ===================
log_info "Enabling SSH..."

# Create empty 'ssh' file to enable SSH on first boot
if mountpoint -q "${MOUNT_BOOT}" 2>/dev/null; then
  sudo touch "${MOUNT_BOOT}/ssh"
  log_success "  Created ssh file in boot partition"
else
  if [[ -d "${MOUNT_ROOT}/boot/firmware" ]]; then
    sudo touch "${MOUNT_ROOT}/boot/firmware/ssh"
    log_success "  Created ssh file in /boot/firmware"
  fi
fi

# Also enable SSH service directly
sudo ln -sf /lib/systemd/system/ssh.service \
  "${MOUNT_ROOT}/etc/systemd/system/multi-user.target.wants/ssh.service" 2>/dev/null || true

# =================== Set hostname ===================
log_info "Setting hostname to 'maschinepi'..."

echo "maschinepi" | sudo tee "${MOUNT_ROOT}/etc/hostname" > /dev/null

# Update /etc/hosts
sudo sed -i 's/raspberrypi/maschinepi/g' "${MOUNT_ROOT}/etc/hosts" 2>/dev/null || true
log_success "  Hostname set to maschinepi"

# =================== Configure autologin ===================
log_info "Configuring autologin for 'mpi' user..."

# Create getty override directory
sudo mkdir -p "${MOUNT_ROOT}/etc/systemd/system/getty@tty1.service.d"

# Create autologin override
cat << 'EOF' | sudo tee "${MOUNT_ROOT}/etc/systemd/system/getty@tty1.service.d/autologin.conf" > /dev/null
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin mpi --noclear %I $TERM
EOF
log_success "  Configured autologin for mpi on tty1"

# Also create serial console autologin for headless use
sudo mkdir -p "${MOUNT_ROOT}/etc/systemd/system/serial-getty@ttyS0.service.d"
cat << 'EOF' | sudo tee "${MOUNT_ROOT}/etc/systemd/system/serial-getty@ttyS0.service.d/autologin.conf" > /dev/null
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin mpi --noclear %I 115200 $TERM
EOF
log_success "  Configured autologin for serial console"

# =================== WiFi configuration placeholder ===================
log_info "Creating WiFi configuration template..."

# Create maschinepi config directory
sudo mkdir -p "${MOUNT_ROOT}/boot/firmware/maschinepi"

# Create WiFi config template
cat << 'EOF' | sudo tee "${MOUNT_ROOT}/boot/firmware/maschinepi/wifi.txt" > /dev/null
# MaschinePI WiFi Configuration
# Edit this file before first boot to configure WiFi
#
# Uncomment and fill in your WiFi details:
# WIFI_SSID="YourNetworkName"
# WIFI_PASSWORD="YourPassword"
# WIFI_COUNTRY="US"
#
# Country codes: US, GB, DE, FR, etc.
# After editing, the first-boot script will configure WiFi automatically.
EOF
log_success "  Created wifi.txt template in /boot/firmware/maschinepi/"

# =================== Unmount ===================
log_info "Syncing and unmounting..."

sync

if mountpoint -q "${MOUNT_BOOT}" 2>/dev/null; then
  sudo umount "${MOUNT_BOOT}"
fi
sudo umount "${MOUNT_ROOT}"

sudo losetup -d "${LOOP_DEVICE}"
LOOP_DEVICE=""

# Clear mount vars so cleanup doesn't double-unmount
MOUNT_BOOT=""
MOUNT_ROOT=""

# =================== Compress if requested ===================
FINAL_IMG="${WORK_IMG}"

if [[ "${COMPRESS}" == "1" ]]; then
  log_info "Compressing image..."
  xz -9 -T0 "${WORK_IMG}"
  FINAL_IMG="${WORK_IMG}.xz"
fi

# =================== Summary ===================
echo ""
echo "=========================================="
echo "  MaschinePI image created successfully!"
echo "=========================================="
echo ""
echo "  Output: ${FINAL_IMG}"
echo "  Size:   $(du -h "${FINAL_IMG}" | cut -f1)"
echo ""
echo "  Default login:"
echo "    User:     mpi"
echo "    Password: myMP!"
echo "    Hostname: maschinepi"
echo "    SSH:      Enabled"
echo ""
echo "  WiFi setup (optional):"
echo "    1. Mount the boot partition"
echo "    2. Edit maschinepi/wifi.txt with your SSID/password"
echo "    3. Unmount and boot"
echo ""
echo "  Flash to SD card with:"
if [[ "${COMPRESS}" == "1" ]]; then
  echo "    xz -dc ${FINAL_IMG} | sudo dd of=/dev/sdX bs=4M status=progress"
else
  echo "    sudo dd if=${FINAL_IMG} of=/dev/sdX bs=4M status=progress"
fi
echo "    (or use Raspberry Pi Imager)"
echo ""
echo "  First boot will:"
echo "    - Create mpi user with audio permissions"
echo "    - Configure WiFi (if wifi.txt is filled in)"
echo "    - Install PipeWire audio stack"
echo "    - Apply realtime audio optimizations"
echo ""
