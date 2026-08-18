#!/usr/bin/env bash
#
# Flash Raspberry Pi OS image to SD card with user/WiFi configuration
#
# Usage:
#   scripts/flash-sd.sh [image-path]
#
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.."; pwd)"

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

do_flash_sd() {
  local image_path="${1:-}"
  local mount_point=""

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

  for tool in dd lsblk openssl sync partprobe; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
      log_error "Required tool not found: ${tool}"
      exit 1
    fi
  done

  if [[ -z "${image_path}" ]]; then
    echo "Enter path to Raspberry Pi OS Lite image:"
    echo "(e.g., ~/Downloads/2024-raspios-lite-arm64.img.xz)"
    read -rp "> " image_path
  fi

  image_path="${image_path/#\~/$HOME}"

  if [[ ! -f "${image_path}" ]]; then
    log_error "Image not found: ${image_path}"
    exit 1
  fi

  echo ""
  echo "Available removable devices:"
  echo ""
  lsblk -d -o NAME,SIZE,MODEL,TRAN,RM | grep -E "usb|mmc|1$" | grep -v "NAME" || \
    lsblk -d -o NAME,SIZE,MODEL | grep -v "loop\|sr\|nvme\|sda\|NAME"
  echo ""

  echo "Enter SD card device (e.g., sdb, mmcblk0):"
  echo "WARNING: All data on this device will be PERMANENTLY ERASED!"
  read -rp "> " sd_device

  if [[ ! "${sd_device}" =~ ^/dev/ ]]; then
    sd_device="/dev/${sd_device}"
  fi

  if [[ "${sd_device}" =~ mmcblk ]]; then
    sd_device="${sd_device%%p[0-9]*}"
  else
    sd_device="${sd_device%%[0-9]*}"
  fi

  if [[ ! -b "${sd_device}" ]]; then
    log_error "Not a block device: ${sd_device}"
    exit 1
  fi

  local root_device=$(df / | tail -1 | awk '{print $1}' | sed 's/[0-9]*$//' | sed 's/p$//')
  if [[ "${sd_device}" == "${root_device}" ]]; then
    log_error "REFUSING to write to system disk: ${sd_device}"
    exit 1
  fi

  if lsblk -no MOUNTPOINT "${sd_device}" 2>/dev/null | grep -qE "^/$|^/home|^/boot|^/var"; then
    log_error "Device ${sd_device} contains system partitions. Aborting."
    exit 1
  fi

  log_info "Unmounting any mounted partitions on ${sd_device}..."
  for part in "${sd_device}"* "${sd_device}"p*; do
    if [[ -b "${part}" ]] && mountpoint -q "$(lsblk -no MOUNTPOINT "${part}" 2>/dev/null)" 2>/dev/null; then
      sudo umount "${part}" 2>/dev/null || true
    fi
  done
  sudo umount "${sd_device}"?* 2>/dev/null || true
  sudo umount "${sd_device}"p?* 2>/dev/null || true
  sleep 1

  echo ""
  echo "Enter username for Pi (default: mpi):"
  read -rp "> " pi_user
  pi_user="${pi_user:-mpi}"

  if [[ ! "${pi_user}" =~ ^[a-z_][a-z0-9_-]*$ ]]; then
    log_error "Invalid username. Use lowercase letters, numbers, underscore, hyphen."
    exit 1
  fi

  echo ""
  echo "Enter password for ${pi_user}:"
  read -rs pi_password
  echo ""

  if [[ -z "${pi_password}" ]]; then
    log_error "Password cannot be empty"
    exit 1
  fi

  echo "Confirm password:"
  read -rs pi_password_confirm
  echo ""

  if [[ "${pi_password}" != "${pi_password_confirm}" ]]; then
    log_error "Passwords do not match"
    exit 1
  fi

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
    wifi_country="${wifi_country^^}"
  fi

  echo ""
  echo "=========================================="
  echo "  Target Device Info"
  echo "=========================================="
  lsblk -o NAME,SIZE,MODEL,SERIAL "${sd_device}" 2>/dev/null || lsblk "${sd_device}"
  echo ""

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

  sudo umount "${sd_device}"?* 2>/dev/null || true
  sudo umount "${sd_device}"p?* 2>/dev/null || true
  sync

  log_info "Writing image to ${sd_device}..."
  log_info "This will take several minutes. Do not remove the SD card."
  echo ""

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

  log_info "Syncing buffers to disk..."
  sync; sudo sync; sleep 1; sync

  log_success "Image written successfully"

  log_info "Reading partition table..."
  sudo partprobe "${sd_device}" 2>/dev/null || true
  sleep 2
  sudo partprobe "${sd_device}" 2>/dev/null || true

  local boot_part=""
  for i in {1..10}; do
    if [[ -b "${sd_device}1" ]]; then
      boot_part="${sd_device}1"; break
    elif [[ -b "${sd_device}p1" ]]; then
      boot_part="${sd_device}p1"; break
    fi
    sleep 1
  done

  if [[ -z "${boot_part}" ]]; then
    log_error "Could not find boot partition after 10 seconds"
    ls -la "${sd_device}"* 2>/dev/null || true
    exit 1
  fi

  local root_part=""
  if [[ -b "${sd_device}2" ]]; then
    root_part="${sd_device}2"
  elif [[ -b "${sd_device}p2" ]]; then
    root_part="${sd_device}p2"
  fi

  log_info "Found partitions: boot=${boot_part}, root=${root_part:-none}"

  local boot_mount=$(mktemp -d)
  local root_mount=$(mktemp -d)

  flash_cleanup() {
    mountpoint -q "${boot_mount}" 2>/dev/null && sudo umount "${boot_mount}" 2>/dev/null
    mountpoint -q "${root_mount}" 2>/dev/null && sudo umount "${root_mount}" 2>/dev/null
    rmdir "${boot_mount}" "${root_mount}" 2>/dev/null || true
  }
  trap flash_cleanup EXIT

  sleep 2

  log_info "Mounting boot partition..."
  if ! sudo mount "${boot_part}" "${boot_mount}"; then
    log_error "Failed to mount boot partition"
    exit 1
  fi

  if [[ -n "${root_part}" ]]; then
    log_info "Mounting root partition..."
    if ! sudo mount "${root_part}" "${root_mount}"; then
      log_warn "Failed to mount root partition - WiFi may need manual config"
      root_part=""
    fi
  fi

  log_info "Enabling SSH..."
  sudo touch "${boot_mount}/ssh"

  log_info "Configuring user ${pi_user}..."
  local encrypted_pw=$(echo "${pi_password}" | openssl passwd -6 -stdin)
  echo "${pi_user}:${encrypted_pw}" | sudo tee "${boot_mount}/userconf.txt" > /dev/null

  if [[ -n "${wifi_ssid}" ]]; then
    log_info "Configuring WiFi for ${wifi_ssid}..."

    cat << CUSTOMTOML | sudo tee "${boot_mount}/custom.toml" > /dev/null
# Raspberry Pi OS first-boot configuration
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

    if [[ -n "${root_part}" ]] && mountpoint -q "${root_mount}"; then
      local nm_dir="${root_mount}/etc/NetworkManager/system-connections"
      sudo mkdir -p "${nm_dir}"
      local conn_uuid=$(cat /proc/sys/kernel/random/uuid 2>/dev/null || uuidgen 2>/dev/null || echo "$(date +%s)-wifi")

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

      sudo chmod 600 "${nm_dir}/${wifi_ssid}.nmconnection"
      sudo chown root:root "${nm_dir}/${wifi_ssid}.nmconnection"
      log_success "Created NetworkManager connection file"

      local reg_file="${root_mount}/etc/default/crda"
      if [[ -f "${reg_file}" ]]; then
        sudo sed -i "s/^REGDOMAIN=.*/REGDOMAIN=${wifi_country}/" "${reg_file}"
      fi
    fi

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

  if [[ -n "${root_part}" ]] && mountpoint -q "${root_mount}"; then
    log_info "Setting hostname to maschinepi..."
    echo "maschinepi" | sudo tee "${root_mount}/etc/hostname" > /dev/null
    if [[ -f "${root_mount}/etc/hosts" ]]; then
      sudo sed -i 's/raspberrypi/maschinepi/g' "${root_mount}/etc/hosts"
    fi
    log_success "Hostname configured"
  fi

  log_info "Syncing filesystems..."
  sync; sudo sync; sleep 1

  if mountpoint -q "${root_mount}" 2>/dev/null; then
    log_info "Unmounting root partition..."
    sudo umount "${root_mount}" || sudo umount -l "${root_mount}" || true
  fi
  rmdir "${root_mount}" 2>/dev/null || true

  log_info "Unmounting boot partition..."
  if ! sudo umount "${boot_mount}"; then
    log_warn "Normal unmount failed, trying lazy unmount..."
    sudo umount -l "${boot_mount}" || true
  fi
  rmdir "${boot_mount}" 2>/dev/null || true

  trap - EXIT

  log_info "Final sync..."
  sync; sudo sync; sleep 2; sync

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
  echo "    5. SSH in:"
  echo "         ssh ${pi_user}@maschinepi.local"
  echo ""
}

do_flash_sd "$@"
