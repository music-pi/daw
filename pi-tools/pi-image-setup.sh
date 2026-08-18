#!/usr/bin/env bash
set -euo pipefail

# Script run inside chroot to set up the Raspberry Pi image
# This installs dependencies, builds maschinepi, and configures the system

AUTO_START=1
SKIP_BUILD=0
ROOT_DIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --auto-start) AUTO_START="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD="$2"; shift 2 ;;
    --root-dir) ROOT_DIR="$2"; shift 2 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

echo "=========================================="
echo "  MaschinePI Image Setup"
echo "=========================================="

# =================== Update package lists ===============
echo "==> Updating package lists..."
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq

# =================== Install dependencies ================
echo "==> Installing system dependencies..."
apt-get install -y -qq $(cat /tmp/pi-dependencies.txt | grep -v '^#' | grep -v '^$' | tr '\n' ' ')

# =================== Configure realtime audio ===========
echo "==> Configuring realtime audio optimizations..."
bash /tmp/realtime-config.sh /etc

# =================== Build maschinepi ===================
if [[ "$SKIP_BUILD" == "0" && -n "$ROOT_DIR" && -d "$ROOT_DIR" ]]; then
    echo "==> Building maschinepi application..."
    cd "$ROOT_DIR"
    
    # Configure and build
    mkdir -p build-pi
    cd build-pi
    
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DDEV_DESKTOP=OFF \
        -DENABLE_TRACKTION=ON \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    
    cmake --build . -j$(nproc)
    
    # Find and install binary (JUCE may put it in different locations)
    if [[ -f maschinepi ]]; then
        install -D -m 755 maschinepi /usr/local/bin/maschinepi
    elif [[ -f maschinepi_artefacts/Release/MaschinePI ]]; then
        install -D -m 755 maschinepi_artefacts/Release/MaschinePI /usr/local/bin/maschinepi
    elif [[ -f maschinepi_artefacts/Release/Standalone/MaschinePI ]]; then
        install -D -m 755 maschinepi_artefacts/Release/Standalone/MaschinePI /usr/local/bin/maschinepi
    else
        echo "ERROR: Could not find maschinepi binary after build"
        exit 1
    fi
    
    echo "==> maschinepi built and installed"
else
    echo "==> Skipping build (SKIP_BUILD=1 or ROOT_DIR not set)"
    if [[ ! -f /usr/local/bin/maschinepi ]]; then
        echo "WARNING: maschinepi binary not found. Install manually or set ROOT_DIR."
    fi
fi

# =================== Build mk3-boot-display ============
echo "==> Building mk3-boot-display helper..."
cd /tmp

# Build mk3-boot-display with libmk3 from the project
if [[ "$SKIP_BUILD" == "0" && -n "$ROOT_DIR" && -d "$ROOT_DIR" ]]; then
    # Try to find libmk3 from maschinepi build
    mk3_lib="${ROOT_DIR}/build-pi/external/mk3/libmk3.a"
    
    if [[ ! -f "$mk3_lib" ]]; then
        echo "WARNING: libmk3.a not found. Building mk3 library directly..."
        cd "${ROOT_DIR}/external/mk3"
        
        # Get libusb flags
        usb_cflags=$(pkg-config --cflags libusb-1.0 2>/dev/null || echo "-I/usr/include/libusb-1.0")
        usb_libs=$(pkg-config --libs libusb-1.0 2>/dev/null || echo "-lusb-1.0")
        
        # Simple direct build of mk3 library
        gcc -c -o mk3.o mk3.c -I. $usb_cflags -std=c11 -Wall -O2 -fPIC
        gcc -c -o mk3_display.o mk3_display.c -I. $usb_cflags -std=c11 -Wall -O2 -fPIC
        gcc -c -o mk3_input.o mk3_input.c -I. $usb_cflags -std=c11 -Wall -O2 -fPIC
        gcc -c -o mk3_input_map.o mk3_input_map.c -I. -std=c11 -Wall -O2 -fPIC
        gcc -c -o mk3_output.o mk3_output.c -I. $usb_cflags -std=c11 -Wall -O2 -fPIC
        gcc -c -o mk3_output_map.o mk3_output_map.c -I. -std=c11 -Wall -O2 -fPIC
        
        ar rcs libmk3.a mk3.o mk3_display.o mk3_input.o mk3_input_map.o mk3_output.o mk3_output_map.o
        mk3_lib="${ROOT_DIR}/external/mk3/libmk3.a"
    fi
    
    # Copy source files to /tmp
    cp "${ROOT_DIR}/pi-tools/mk3-boot-display.c" /tmp/
    if [[ -f "${ROOT_DIR}/pi-tools/mk3-boot-listener.c" ]]; then
        cp "${ROOT_DIR}/pi-tools/mk3-boot-listener.c" /tmp/
    fi
    if [[ -f "${ROOT_DIR}/pi-tools/maschinepi-system-config.c" ]]; then
        cp "${ROOT_DIR}/pi-tools/maschinepi-system-config.c" /tmp/
    fi
    
    # Compile mk3-boot-display
    cd /tmp
    usb_cflags=$(pkg-config --cflags libusb-1.0 2>/dev/null || echo "-I/usr/include/libusb-1.0")
    usb_libs=$(pkg-config --libs libusb-1.0 2>/dev/null || echo "-lusb-1.0")
    
    gcc -o mk3-boot-display mk3-boot-display.c \
        -I"${ROOT_DIR}/external/mk3" \
        $usb_cflags \
        "$mk3_lib" \
        $usb_libs \
        -std=c11 -Wall -O2 -lm
    
    if [[ -f mk3-boot-display ]]; then
        install -D -m 755 mk3-boot-display /usr/local/bin/mk3-boot-display
        echo "==> mk3-boot-display built and installed"
    else
        echo "ERROR: Failed to build mk3-boot-display"
        exit 1
    fi
    
    # Build mk3-boot-listener
    if [[ -f "${ROOT_DIR}/pi-tools/mk3-boot-listener.c" ]]; then
        echo "==> Building mk3-boot-listener..."
        gcc -o mk3-boot-listener mk3-boot-listener.c \
            -I"${ROOT_DIR}/external/mk3" \
            $usb_cflags \
            "$mk3_lib" \
            $usb_libs \
            -std=c11 -Wall -O2 -lm
        
        if [[ -f mk3-boot-listener ]]; then
            install -D -m 755 mk3-boot-listener /usr/local/bin/mk3-boot-listener
            echo "==> mk3-boot-listener built and installed"
        else
            echo "ERROR: Failed to build mk3-boot-listener"
            exit 1
        fi
    fi
    
    # Build maschinepi-system-config
    if [[ -f "${ROOT_DIR}/pi-tools/maschinepi-system-config.c" ]]; then
        echo "==> Building maschinepi-system-config..."
        gcc -o maschinepi-system-config maschinepi-system-config.c \
            -I"${ROOT_DIR}/external/mk3" \
            $usb_cflags \
            "$mk3_lib" \
            $usb_libs \
            -std=c11 -Wall -O2 -lm
        
        if [[ -f maschinepi-system-config ]]; then
            install -D -m 755 maschinepi-system-config /usr/local/bin/maschinepi-system-config
            echo "==> maschinepi-system-config built and installed"
        else
            echo "ERROR: Failed to build maschinepi-system-config"
            exit 1
        fi
    fi
else
    echo "==> Skipping mk3-boot-display build"
fi

# =================== Install utility scripts ============
echo "==> Installing utility scripts..."
mkdir -p /opt/maschinepi/scripts

# Copy scripts from source (if available) or create symlinks
if [[ -n "$ROOT_DIR" && -d "$ROOT_DIR/scripts" ]]; then
    cp -r "$ROOT_DIR/scripts"/* /opt/maschinepi/scripts/
    chmod +x /opt/maschinepi/scripts/*.sh
fi

# =================== Install systemd services ==========
echo "==> Installing systemd services..."

# Install mk3-boot-display service
install -D -m 644 /tmp/mk3-boot-display.service /etc/systemd/system/mk3-boot-display.service

# Install mk3-boot-listener service
if [[ -f "${ROOT_DIR}/pi-tools/mk3-boot-listener.service" ]]; then
    cp "${ROOT_DIR}/pi-tools/mk3-boot-listener.service" /tmp/
    install -D -m 644 /tmp/mk3-boot-listener.service /etc/systemd/system/mk3-boot-listener.service
fi

# Install maschinepi-launcher script
if [[ -f "${ROOT_DIR}/pi-tools/maschinepi-launcher.sh" ]]; then
    cp "${ROOT_DIR}/pi-tools/maschinepi-launcher.sh" /tmp/
    install -D -m 755 /tmp/maschinepi-launcher.sh /usr/local/bin/maschinepi-launcher.sh
fi

# Install maschinepi service
if [[ "$AUTO_START" == "1" ]]; then
    install -D -m 644 /tmp/maschinepi.service /etc/systemd/system/maschinepi.service
    systemctl enable maschinepi.service
    echo "==> maschinepi service enabled for auto-start"
fi

# Enable mk3-boot-display service
systemctl enable mk3-boot-display.service

# Enable mk3-boot-listener service
if [[ -f /etc/systemd/system/mk3-boot-listener.service ]]; then
    systemctl enable mk3-boot-listener.service
fi

# =================== Configure user and groups ==========
echo "==> Configuring user and groups..."
# Ensure pi user exists and is in audio group
if id "pi" &>/dev/null; then
    usermod -a -G audio,pipewire,plugdev pi
else
    # Create pi user if it doesn't exist
    useradd -m -s /bin/bash -G audio,pipewire,plugdev pi
fi

# =================== Cleanup ===========================
echo "==> Cleaning up..."
apt-get clean -qq
rm -rf /tmp/* /var/tmp/*

# =================== Finalize ==========================
echo ""
echo "=========================================="
echo "  Setup complete!"
echo "=========================================="
echo ""
echo "Installed:"
echo "  - maschinepi: /usr/local/bin/maschinepi"
echo "  - mk3-boot-display: /usr/local/bin/mk3-boot-display"
echo "  - mk3-boot-listener: /usr/local/bin/mk3-boot-listener"
echo "  - maschinepi-system-config: /usr/local/bin/maschinepi-system-config"
echo "  - maschinepi-launcher: /usr/local/bin/maschinepi-launcher.sh"
echo "  - Utility scripts: /opt/maschinepi/scripts"
echo "  - Systemd services: enabled"
echo ""

