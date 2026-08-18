#!/bin/bash
#
# MaschinePI First-Boot Script
# Installs audio dependencies on first boot, then disables itself
#
set -e

LOG_FILE="/var/log/maschinepi-first-boot.log"

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a "${LOG_FILE}"
}

log "MaschinePI first-boot starting..."

# =================== Configure WiFi if specified ===================
WIFI_CONFIG="/boot/firmware/maschinepi/wifi.txt"
if [[ -f "${WIFI_CONFIG}" ]]; then
    log "Checking WiFi configuration..."

    # Source the config file to get variables
    WIFI_SSID=""
    WIFI_PASSWORD=""
    WIFI_COUNTRY="US"

    # Parse config file (handle commented lines)
    while IFS='=' read -r key value; do
        # Skip comments and empty lines
        [[ "$key" =~ ^[[:space:]]*# ]] && continue
        [[ -z "$key" ]] && continue

        # Remove quotes from value
        value="${value%\"}"
        value="${value#\"}"

        case "$key" in
            WIFI_SSID) WIFI_SSID="$value" ;;
            WIFI_PASSWORD) WIFI_PASSWORD="$value" ;;
            WIFI_COUNTRY) WIFI_COUNTRY="$value" ;;
        esac
    done < "${WIFI_CONFIG}"

    if [[ -n "${WIFI_SSID}" ]] && [[ -n "${WIFI_PASSWORD}" ]]; then
        log "Configuring WiFi for SSID: ${WIFI_SSID}"

        # Set regulatory domain
        iw reg set "${WIFI_COUNTRY}" 2>/dev/null || true

        # Check if NetworkManager is available (Trixie uses NM)
        if command -v nmcli &>/dev/null; then
            log "Using NetworkManager for WiFi..."
            nmcli device wifi connect "${WIFI_SSID}" password "${WIFI_PASSWORD}" || {
                log "WiFi connection failed, will retry after reboot"
            }
        # Fallback to wpa_supplicant for older systems
        elif command -v wpa_passphrase &>/dev/null; then
            log "Using wpa_supplicant for WiFi..."
            WPA_CONF="/etc/wpa_supplicant/wpa_supplicant.conf"
            cat > "${WPA_CONF}" << WPAEOF
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
update_config=1
country=${WIFI_COUNTRY}

$(wpa_passphrase "${WIFI_SSID}" "${WIFI_PASSWORD}")
WPAEOF
            # Enable wpa_supplicant
            systemctl enable wpa_supplicant@wlan0 || true
            systemctl start wpa_supplicant@wlan0 || true
        fi

        log "WiFi configuration complete"
    else
        log "WiFi not configured (SSID or password missing)"
    fi
fi

# Update package lists
log "Updating package lists..."
apt-get update

# Install audio stack (PipeWire)
log "Installing PipeWire audio stack..."
apt-get install -y \
    pipewire \
    pipewire-alsa \
    pipewire-jack \
    pipewire-pulse \
    wireplumber

# Install runtime dependencies
log "Installing runtime dependencies..."
apt-get install -y \
    libusb-1.0-0 \
    libasound2

# Create audio group if it doesn't exist
if ! getent group audio >/dev/null; then
    log "Creating audio group..."
    groupadd audio
fi

# Create pipewire group if it doesn't exist
if ! getent group pipewire >/dev/null; then
    log "Creating pipewire group..."
    groupadd pipewire
fi

# Add mpi user to audio groups
if id "mpi" &>/dev/null; then
    log "Adding mpi user to audio groups..."
    usermod -aG audio,pipewire,plugdev mpi || true
fi

# Apply sysctl settings immediately
log "Applying realtime audio settings..."
sysctl -p /etc/sysctl.d/99-realtime-audio.conf || true

# Reload udev rules
log "Reloading udev rules..."
udevadm control --reload-rules
udevadm trigger

# Clean up apt cache to save space
log "Cleaning up..."
apt-get clean
rm -rf /var/lib/apt/lists/*

# Disable this service so it doesn't run again
log "Disabling first-boot service..."
systemctl disable maschinepi-first-boot.service

log "First-boot complete! Rebooting in 5 seconds..."
sleep 5
reboot
