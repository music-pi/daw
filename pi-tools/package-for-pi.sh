#!/usr/bin/env bash
#
# DEPRECATED: This script uses pi-gen which is slow (30-60 min) and complex.
#
# Use the new simplified workflow instead:
#   1. ./mpi build pi              # Cross-compile for ARM64
#   2. ./mpi gen-img base.img      # Inject into existing image
#
# The new approach takes minutes instead of an hour and doesn't require
# Docker or QEMU. See ./mpi --help for details.
#
set -euo pipefail

# =================== Config & defaults ===================
ROOT_DIR="$(cd "$(dirname "$0")/.."; pwd)"
cd "$ROOT_DIR"

SCRIPT_DIR="$(cd "$(dirname "$0")"; pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT_DIR}/pi-tools/output}"
WORK_DIR="${WORK_DIR:-${ROOT_DIR}/pi-tools/work}"
PI_GEN_DIR="${PI_GEN_DIR:-${WORK_DIR}/pi-gen}"
AUTO_START="${AUTO_START:-1}"
SKIP_BUILD="${SKIP_BUILD:-0}"
COMPRESS="${COMPRESS:-0}"
DRY_RUN="${DRY_RUN:-0}"

# =================== CLI flags ===========================
while [[ $# -gt 0 ]]; do
  case "$1" in
    --output-dir) OUTPUT_DIR="${2}"; shift 2 ;;
    --work-dir) WORK_DIR="${2}"; shift 2 ;;
    --pi-gen-dir) PI_GEN_DIR="${2}"; shift 2 ;;
    --no-auto-start) AUTO_START=0; shift ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    --compress) COMPRESS=1; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --continue) export CONTINUE=1; shift ;;
    *) 
      # Check if it's an environment variable assignment (VAR=value)
      if [[ "$1" =~ ^[A-Z_]+= ]]; then
        export "$1"
        shift
      else
        echo "Unknown arg: $1"
        echo ""
        echo "Usage: $0 [options]"
        echo "Options:"
        echo "  --output-dir DIR     Output directory for image"
        echo "  --work-dir DIR       Working directory"
        echo "  --no-auto-start     Disable auto-start service"
        echo "  --skip-build        Use pre-built binary"
        echo "  --compress          Compress output image"
        echo "  --continue          Continue previous build (same as CONTINUE=1)"
        echo ""
        echo "Environment variables:"
        echo "  CONTINUE=1          Continue previous pi-gen build"
        echo "  CLEAN=1             Clean and rebuild stages (use with CONTINUE=1 to rebuild failed stage)"
        echo "  PRESERVE_CONTAINER=1 Preserve Docker container"
        exit 1
      fi
      ;;
  esac
done

# =================== Check dependencies ===================
check_deps() {
    local missing_required=()
    local install_cmd=""
    
    # Detect package manager
    if command -v apt-get >/dev/null 2>&1; then
        install_cmd="sudo apt-get install"
    elif command -v yum >/dev/null 2>&1; then
        install_cmd="sudo yum install"
    elif command -v dnf >/dev/null 2>&1; then
        install_cmd="sudo dnf install"
    elif command -v pacman >/dev/null 2>&1; then
        install_cmd="sudo pacman -S"
    else
        install_cmd="your-package-manager"
    fi
    
    # Required tools for pi-gen
    declare -A required_tools=(
        ["git"]="git"
        ["docker"]="docker.io"
        ["qemu-aarch64-static"]="qemu-user-static"
    )
    
    echo "==> Checking dependencies..."
    
    for cmd in "${!required_tools[@]}"; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            missing_required+=("${required_tools[$cmd]}")
        fi
    done
    
    if [[ ${#missing_required[@]} -gt 0 ]]; then
        echo ""
        echo "ERROR: Missing required tools:"
        for pkg in "${missing_required[@]}"; do
            echo "  - $pkg"
        done
        echo ""
        
        if [[ "$install_cmd" != "your-package-manager" ]]; then
            local unique_packages=($(printf "%s\n" "${missing_required[@]}" | sort -u))
            echo "Install missing dependencies with:"
            if [[ "$install_cmd" == *"apt-get"* ]]; then
                echo "  $install_cmd update && $install_cmd install -y ${unique_packages[*]}"
            else
                echo "  $install_cmd ${unique_packages[*]}"
            fi
        fi
        echo ""
        exit 1
    fi
    
    # Check for root access (needed for Docker or direct build)
    if [[ "$EUID" -ne 0 ]] && ! docker info >/dev/null 2>&1; then
        echo "WARNING: Not running as root and Docker may not be accessible"
        echo "You may need to run with sudo or configure Docker access"
    fi
    
    echo "==> All required dependencies found"
}

# =================== Setup pi-gen ========================
setup_pi_gen() {
    local pi_gen_dir="$1"
    
    if [[ -d "$pi_gen_dir" && -f "$pi_gen_dir/build.sh" ]]; then
        echo "==> pi-gen already exists at $pi_gen_dir"
        echo "==> Updating pi-gen..."
        cd "$pi_gen_dir"
        git pull origin arm64 || git pull origin master || true
        return 0
    fi
    
    echo "==> Cloning pi-gen repository (arm64 branch)..."
    mkdir -p "$(dirname "$pi_gen_dir")"
    
    if ! git clone --branch arm64 --depth 1 https://github.com/RPI-Distro/pi-gen.git "$pi_gen_dir" 2>/dev/null; then
        echo "WARNING: arm64 branch not found, trying master branch..."
        git clone --depth 1 https://github.com/RPI-Distro/pi-gen.git "$pi_gen_dir"
    fi
    
    if [[ ! -f "$pi_gen_dir/build.sh" ]]; then
        echo "ERROR: Failed to clone pi-gen repository"
        return 1
    fi
    
    echo "==> pi-gen cloned successfully"
}

# =================== Create maschinepi stage ==============
create_maschinepi_stage() {
    local pi_gen_dir="$1"
    # pi-gen stages are numbered - use stage6 for our custom stage (after stage2 lite)
    local stage_dir="${pi_gen_dir}/stage6-maschinepi"
    
    echo "==> Creating maschinepi custom stage..."
    
    # Create single stage directory (pi-gen convention: stage-XX-name)
    mkdir -p "${stage_dir}"
    
    # Create run script (pi-gen executes 00-run.sh in each stage)
    # This script runs inside the chroot environment
    cat > "${stage_dir}/00-run.sh" << 'STAGESCRIPT'
#!/bin/bash
set -e

# pi-gen provides these variables:
# ROOTFS_DIR - path to root filesystem
# STAGE_WORK_DIR - working directory for this stage
# STAGE_DIR - path to this stage directory

# Install required packages using pi-gen's install_packages function
# (This function is provided by pi-gen's common scripts)
if command -v install_packages >/dev/null 2>&1; then
    install_packages pipewire pipewire-alsa pipewire-jack pipewire-pulse wireplumber \
        libasound2 libasound2-dev libusb-1.0-0 libusb-1.0-0-dev \
        build-essential cmake git pkg-config ninja-build
else
    # Fallback if install_packages not available
    apt-get install -y pipewire pipewire-alsa pipewire-jack pipewire-pulse wireplumber \
        libasound2 libasound2-dev libusb-1.0-0 libusb-1.0-0-dev \
        build-essential cmake git pkg-config ninja-build
fi

# Build maschinepi if source is available
# MASCHINEPI_SOURCE is passed as environment variable from main script
if [[ -n "${MASCHINEPI_SOURCE:-}" ]] && [[ -d "${MASCHINEPI_SOURCE}" ]] && [[ "${SKIP_BUILD:-0}" == "0" ]]; then
    echo "==> Building maschinepi..."
    
    BUILD_DIR="${STAGE_WORK_DIR}/maschinepi-build"
    mkdir -p "${BUILD_DIR}"
    
    # Copy source (excluding build artifacts)
    rsync -a --exclude='build' --exclude='.git' --exclude='pi-tools' \
          "${MASCHINEPI_SOURCE}/" "${BUILD_DIR}/"
    
    cd "${BUILD_DIR}"
    mkdir -p build
    cd build
    
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DDEV_DESKTOP=OFF \
        -DENABLE_TRACKTION=ON
    
    cmake --build . -j$(nproc)
    
    # Find and install binary (check multiple possible locations)
    BINARY=""
    for path in maschinepi maschinepi_artefacts/Release/MaschinePI maschinepi_artefacts/Release/Standalone/MaschinePI; do
        if [[ -f "${BUILD_DIR}/build/${path}" ]]; then
            BINARY="${BUILD_DIR}/build/${path}"
            break
        fi
    done
    
    if [[ -n "$BINARY" ]] && [[ -f "$BINARY" ]]; then
        install -D -m 755 "$BINARY" "${ROOTFS_DIR}/usr/local/bin/maschinepi"
        echo "==> maschinepi installed"
    else
        echo "WARNING: Could not find maschinepi binary after build"
    fi
    
    # Build mk3-boot-display
    if [[ -d "${MASCHINEPI_SOURCE}/external/mk3" ]] && [[ -f "${MASCHINEPI_SOURCE}/pi-tools/mk3-boot-display.c" ]]; then
        echo "==> Building mk3-boot-display..."
        cd "${MASCHINEPI_SOURCE}/external/mk3"
        
        # Build mk3 library
        gcc -c -o mk3.o mk3.c -I. $(pkg-config --cflags libusb-1.0) -std=c11 -Wall -O2 -fPIC
        gcc -c -o mk3_display.o mk3_display.c -I. $(pkg-config --cflags libusb-1.0) -std=c11 -Wall -O2 -fPIC
        gcc -c -o mk3_input.o mk3_input.c -I. $(pkg-config --cflags libusb-1.0) -std=c11 -Wall -O2 -fPIC
        gcc -c -o mk3_input_map.o mk3_input_map.c -I. -std=c11 -Wall -O2 -fPIC
        gcc -c -o mk3_output.o mk3_output.c -I. $(pkg-config --cflags libusb-1.0) -std=c11 -Wall -O2 -fPIC
        gcc -c -o mk3_output_map.o mk3_output_map.c -I. -std=c11 -Wall -O2 -fPIC
        ar rcs libmk3.a mk3.o mk3_display.o mk3_input.o mk3_input_map.o mk3_output.o mk3_output_map.o
        
        # Build mk3-boot-display
        gcc -o mk3-boot-display "${MASCHINEPI_SOURCE}/pi-tools/mk3-boot-display.c" \
            -I. $(pkg-config --cflags libusb-1.0) \
            libmk3.a $(pkg-config --libs libusb-1.0) \
            -std=c11 -Wall -O2 -lm
        
        install -D -m 755 mk3-boot-display "${ROOTFS_DIR}/usr/local/bin/mk3-boot-display"
        echo "==> mk3-boot-display installed"
        
        # Build mk3-boot-listener
        if [[ -f "${MASCHINEPI_SOURCE}/pi-tools/mk3-boot-listener.c" ]]; then
            echo "==> Building mk3-boot-listener..."
            gcc -o mk3-boot-listener "${MASCHINEPI_SOURCE}/pi-tools/mk3-boot-listener.c" \
                -I. $(pkg-config --cflags libusb-1.0) \
                libmk3.a $(pkg-config --libs libusb-1.0) \
                -std=c11 -Wall -O2 -lm
            
            install -D -m 755 mk3-boot-listener "${ROOTFS_DIR}/usr/local/bin/mk3-boot-listener"
            echo "==> mk3-boot-listener installed"
        fi
        
        # Build maschinepi-system-config
        if [[ -f "${MASCHINEPI_SOURCE}/pi-tools/maschinepi-system-config.c" ]]; then
            echo "==> Building maschinepi-system-config..."
            gcc -o maschinepi-system-config "${MASCHINEPI_SOURCE}/pi-tools/maschinepi-system-config.c" \
                -I. $(pkg-config --cflags libusb-1.0) \
                libmk3.a $(pkg-config --libs libusb-1.0) \
                -std=c11 -Wall -O2 -lm
            
            install -D -m 755 maschinepi-system-config "${ROOTFS_DIR}/usr/local/bin/maschinepi-system-config"
            echo "==> maschinepi-system-config installed"
        fi
    fi
fi

# Install pre-built binary if provided
if [[ -n "${MASCHINEPI_BINARY:-}" ]] && [[ -f "${MASCHINEPI_BINARY}" ]]; then
    install -D -m 755 "${MASCHINEPI_BINARY}" "${ROOTFS_DIR}/usr/local/bin/maschinepi"
    echo "==> Pre-built maschinepi installed"
fi

# Install utility scripts
if [[ -n "${MASCHINEPI_SOURCE:-}" ]] && [[ -d "${MASCHINEPI_SOURCE}/scripts" ]]; then
    mkdir -p "${ROOTFS_DIR}/opt/maschinepi/scripts"
    cp -r "${MASCHINEPI_SOURCE}/scripts"/* "${ROOTFS_DIR}/opt/maschinepi/scripts/"
    chmod +x "${ROOTFS_DIR}/opt/maschinepi/scripts"/*.sh
fi
STAGESCRIPT
    chmod +x "${stage_dir}/00-run.sh"
    
    # Create files directory for static files
    mkdir -p "${stage_dir}/files"
    
    # Copy realtime config script
    if [[ -f "${SCRIPT_DIR}/realtime-config.sh" ]]; then
        cp "${SCRIPT_DIR}/realtime-config.sh" "${stage_dir}/files/"
    fi
    
    # Copy systemd services
    mkdir -p "${stage_dir}/files/etc/systemd/system"
    if [[ -f "${SCRIPT_DIR}/maschinepi.service" ]]; then
        cp "${SCRIPT_DIR}/maschinepi.service" "${stage_dir}/files/etc/systemd/system/"
    fi
    if [[ -f "${SCRIPT_DIR}/mk3-boot-display.service" ]]; then
        cp "${SCRIPT_DIR}/mk3-boot-display.service" "${stage_dir}/files/etc/systemd/system/"
    fi
    if [[ -f "${SCRIPT_DIR}/mk3-boot-listener.service" ]]; then
        cp "${SCRIPT_DIR}/mk3-boot-listener.service" "${stage_dir}/files/etc/systemd/system/"
    fi
    
    # Copy launcher script
    if [[ -f "${SCRIPT_DIR}/maschinepi-launcher.sh" ]]; then
        mkdir -p "${stage_dir}/files/usr/local/bin"
        cp "${SCRIPT_DIR}/maschinepi-launcher.sh" "${stage_dir}/files/usr/local/bin/"
        chmod +x "${stage_dir}/files/usr/local/bin/maschinepi-launcher.sh"
    fi
    
    # Create prerun script to copy files (pi-gen convention)
    # prerun.sh runs before 00-run.sh, outside chroot
    cat > "${stage_dir}/prerun.sh" << 'PRERUN'
#!/bin/bash -e
# Copy static files to rootfs
# pi-gen provides: STAGE_DIR, ROOTFS_DIR, PREV_ROOTFS_DIR, WORK_DIR, copy_previous function

# Ensure rootfs exists (copy from previous stage if needed)
if [ ! -d "${ROOTFS_DIR}" ]; then
    # If PREV_ROOTFS_DIR is not set or doesn't exist, find the last stage's rootfs
    if [ -z "${PREV_ROOTFS_DIR:-}" ] || [ ! -d "${PREV_ROOTFS_DIR}" ]; then
        # Find the last stage directory that has a rootfs (stages are numbered)
        PREV_ROOTFS_DIR=""
        for stage_num in 5 4 3 2 1 0; do
            stage_path="${WORK_DIR}/stage${stage_num}/rootfs"
            if [ -d "${stage_path}" ]; then
                PREV_ROOTFS_DIR="${stage_path}"
                break
            fi
        done
        
        # If still not found, try stage2 (most common base for lite images)
        if [ -z "${PREV_ROOTFS_DIR:-}" ]; then
            stage2_path="${WORK_DIR}/stage2/rootfs"
            if [ -d "${stage2_path}" ]; then
                PREV_ROOTFS_DIR="${stage2_path}"
            fi
        fi
    fi
    
    # Now try to copy from previous stage
    if [ -n "${PREV_ROOTFS_DIR:-}" ] && [ -d "${PREV_ROOTFS_DIR}" ]; then
        mkdir -p "${ROOTFS_DIR}"
        rsync -aHAXx --exclude var/cache/apt/archives "${PREV_ROOTFS_DIR}/" "${ROOTFS_DIR}/"
    else
        echo "ERROR: Could not find previous stage rootfs"
        echo "  PREV_ROOTFS_DIR: ${PREV_ROOTFS_DIR:-<not set>}"
        echo "  WORK_DIR: ${WORK_DIR:-<not set>}"
        echo "  Looking for rootfs in: ${WORK_DIR}/stage*/rootfs"
        exit 1
    fi
fi

# Copy static files to rootfs
if [[ -d "${STAGE_DIR}/files" ]]; then
    mkdir -p "${ROOTFS_DIR}"
    cp -r "${STAGE_DIR}/files"/* "${ROOTFS_DIR}/"
fi
PRERUN
    chmod +x "${stage_dir}/prerun.sh"
    
    # Create postrun script for system configuration
    # This runs after 00-run.sh, inside chroot
    cat > "${stage_dir}/00-run.sh.post" << 'POSTRUN'
#!/bin/bash
set -e

# Apply realtime audio optimizations
# Note: In postrun, we're still in chroot, so paths are relative to rootfs
if [[ -f "/realtime-config.sh" ]]; then
    bash /realtime-config.sh /etc
    rm -f /realtime-config.sh
fi

# Enable systemd services
# In postrun, we're in chroot, so paths are relative to /
if [[ "${AUTO_START:-1}" == "1" ]] && [[ -f "/etc/systemd/system/maschinepi.service" ]]; then
    mkdir -p /etc/systemd/system/multi-user.target.wants
    ln -sf /etc/systemd/system/maschinepi.service \
        /etc/systemd/system/multi-user.target.wants/maschinepi.service
fi

if [[ -f "/etc/systemd/system/mk3-boot-display.service" ]]; then
    mkdir -p /etc/systemd/system/multi-user.target.wants
    ln -sf /etc/systemd/system/mk3-boot-display.service \
        /etc/systemd/system/multi-user.target.wants/mk3-boot-display.service
fi

if [[ -f "/etc/systemd/system/mk3-boot-listener.service" ]]; then
    mkdir -p /etc/systemd/system/multi-user.target.wants
    ln -sf /etc/systemd/system/mk3-boot-listener.service \
        /etc/systemd/system/multi-user.target.wants/mk3-boot-listener.service
fi

# Configure udev rules for MK3
mkdir -p /etc/udev/rules.d
cat > /etc/udev/rules.d/99-mk3-controller.rules << 'UDEVEOF'
# MK3 Controller USB device rules
SUBSYSTEM=="usb", ATTRS{idVendor}=="17cc", ATTRS{idProduct}=="1600", MODE="0664", GROUP="audio"
UDEVEOF

# Ensure audio group exists
if ! grep -q "^audio:" /etc/group 2>/dev/null; then
    echo "audio:x:29:" >> /etc/group
fi

# Apply realtime kernel optimizations
mkdir -p /etc/sysctl.d
cat > /etc/sysctl.d/99-realtime-audio.conf << 'SYSCTLEOF'
# Realtime audio optimizations
vm.swappiness=10
vm.dirty_ratio=3
vm.dirty_background_ratio=1
kernel.sched_rt_runtime_us=-1
SYSCTLEOF

# Configure CPU governor for performance
mkdir -p /etc/systemd/system
cat > /etc/systemd/system/cpu-performance.service << 'SERVICEEOF'
[Unit]
Description=Set CPU governor to performance
After=multi-user.target

[Service]
Type=oneshot
ExecStart=/bin/bash -c 'echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor'
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
SERVICEEOF

mkdir -p /etc/systemd/system/multi-user.target.wants
ln -sf /etc/systemd/system/cpu-performance.service \
    /etc/systemd/system/multi-user.target.wants/cpu-performance.service
POSTRUN
    chmod +x "${stage_dir}/00-run.sh.post"
    
    echo "==> maschinepi stage created"
}

# =================== Configure pi-gen ====================
configure_pi_gen() {
    local pi_gen_dir="$1"
    
    echo "==> Configuring pi-gen..."
    
    cd "$pi_gen_dir"
    
    # Create config file
    cat > config << EOF
IMG_NAME=maschinepi
RELEASE=trixie
DEPLOY_COMPRESSION=xz
LOCALE_DEFAULT=en_US.UTF-8
TARGET_HOSTNAME=maschinepi
FIRST_USER_NAME=mpi
FIRST_USER_PASS=maschinepi
ENABLE_SSH=1
DISABLE_FIRST_BOOT_USER_RENAME=1
EOF
    
    # Enable auto-login for console (Lite images don't have desktop)
    # This is done by creating a getty override
    mkdir -p stage2/02-net-tweaks/files/etc/systemd/system/getty@tty1.service.d
    cat > stage2/02-net-tweaks/files/etc/systemd/system/getty@tty1.service.d/autologin.conf << 'AUTOLOGINEOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin mpi --noclear %I \$TERM
AUTOLOGINEOF
    
    # Skip stages we don't need (desktop, etc.)
    touch stage3/SKIP
    touch stage4/SKIP
    touch stage5/SKIP
    touch stage4/SKIP_IMAGES
    touch stage5/SKIP_IMAGES
    
    echo "==> pi-gen configured"
}

# =================== Build image ==========================
build_image() {
    local pi_gen_dir="$1"
    
    echo "==> Building Raspberry Pi OS image with pi-gen..."
    echo "==> This will take a while..."
    
    cd "$pi_gen_dir"
    
    # Set environment variables for maschinepi stage
    # These will be available in the stage scripts
    export MASCHINEPI_SOURCE="$ROOT_DIR"
    export MASCHINEPI_BINARY="${MASCHINEPI_BINARY:-}"
    export AUTO_START="$AUTO_START"
    export SKIP_BUILD="$SKIP_BUILD"
    
    # Build using Docker (recommended for pi-gen)
    if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
        echo "==> Building with Docker..."
        if [[ "${CONTINUE:-0}" == "1" ]]; then
            echo "==> Continuing previous build..."
        else
            echo "==> Starting new build..."
        fi
        echo "==> This will take 30-60 minutes depending on your system..."
        # Pass through pi-gen environment variables (CONTINUE, PRESERVE_CONTAINER, CLEAN, etc.)
        PRESERVE_CONTAINER="${PRESERVE_CONTAINER:-0}" \
        CONTINUE="${CONTINUE:-0}" \
        CLEAN="${CLEAN:-0}" \
        ./build-docker.sh
    else
        echo "==> Building directly (requires root)..."
        if [[ "$EUID" -ne 0 ]]; then
            echo "ERROR: Direct build requires root access. Use Docker or run with sudo"
            echo ""
            echo "To use Docker, ensure Docker is installed and your user is in the docker group:"
            echo "  sudo usermod -aG docker $USER"
            echo "  (then log out and back in)"
            return 1
        fi
        echo "==> This will take 30-60 minutes depending on your system..."
        # Pass through pi-gen environment variables
        CONTINUE="${CONTINUE:-0}" \
        CLEAN="${CLEAN:-0}" \
        ./build.sh
    fi
    
    echo "==> Build complete"
}

# =================== Copy output ==========================
copy_output() {
    local pi_gen_dir="$1"
    local output_dir="$2"
    
    echo "==> Copying output image..."
    
    mkdir -p "$output_dir"
    
    # Find the generated image
    # pi-gen outputs to deploy/ directory
    local deploy_dir="${pi_gen_dir}/deploy"
    local img_file=""
    
    # Look for image files (pi-gen names them based on IMG_NAME)
    img_file=$(find "$deploy_dir" -maxdepth 1 -name "*.img" -o -name "*.img.xz" 2>/dev/null | head -1)
    
    if [[ -z "$img_file" ]]; then
        # Try alternative locations
        img_file=$(find "$deploy_dir" -name "maschinepi*.img" -o -name "maschinepi*.img.xz" 2>/dev/null | head -1)
    fi
    
    if [[ -z "$img_file" ]]; then
        echo "ERROR: Could not find generated image in $deploy_dir"
        return 1
    fi
    
    local final_img="${output_dir}/maschinepi-raspios-$(date +%Y%m%d)"
    
    if [[ "$img_file" == *.xz ]]; then
        if [[ "$COMPRESS" == "1" ]]; then
            cp "$img_file" "${final_img}.img.xz"
            final_img="${final_img}.img.xz"
        else
            xz -dc "$img_file" > "${final_img}.img"
            final_img="${final_img}.img"
        fi
    else
        cp "$img_file" "${final_img}.img"
        if [[ "$COMPRESS" == "1" ]]; then
            echo "==> Compressing image..."
            xz -9 "${final_img}.img"
            final_img="${final_img}.img.xz"
        fi
    fi
    
    echo ""
    echo "=========================================="
    echo "  Image created successfully!"
    echo "  Output: $final_img"
    echo "  Size: $(du -h "$final_img" | cut -f1)"
    echo "=========================================="
    echo ""
    echo "Flash to SD card with:"
    echo "  sudo dd if=$final_img of=/dev/sdX bs=4M status=progress"
    echo "  or use Raspberry Pi Imager"
}

# =================== Main ================================
main() {
    if [[ "$DRY_RUN" == "1" ]]; then
        echo "DRY RUN MODE - No changes will be made"
        echo "Would use pi-gen directory: $PI_GEN_DIR"
        echo "Would create output in: $OUTPUT_DIR"
        check_deps
        return 0
    fi
    
    check_deps
    
    mkdir -p "$OUTPUT_DIR" "$WORK_DIR"
    
    # Setup pi-gen
    if ! setup_pi_gen "$PI_GEN_DIR"; then
        echo "ERROR: Failed to setup pi-gen"
        exit 1
    fi
    
    # Create maschinepi stage
    create_maschinepi_stage "$PI_GEN_DIR"
    
    # Configure pi-gen
    configure_pi_gen "$PI_GEN_DIR"
    
    # Build image
    if ! build_image "$PI_GEN_DIR"; then
        echo "ERROR: Build failed"
        exit 1
    fi
    
    # Copy output
    copy_output "$PI_GEN_DIR" "$OUTPUT_DIR"
}

main "$@"
