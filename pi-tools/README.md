# MaschinePI Raspberry Pi Image Packaging Tools

> **Legacy packaging path:** the supported entry point is now
> `./mpi gen-img` from the repository root, which delegates to
> `scripts/gen-img.sh`. The scripts in this directory remain useful for the
> older pi-gen workflow and existing image artefacts.

This directory contains tools for creating a flashable Raspberry Pi OS image with MaschinePI pre-installed and configured for realtime audio/DAW work using the official **pi-gen** image builder.

## Overview

The packaging system uses [pi-gen](https://github.com/RPI-Distro/pi-gen) (the official Raspberry Pi OS image builder) to create a custom Raspberry Pi OS Lite ARM64 image that includes:

- Pre-built MaschinePI application (ARM64 Release build)
- PipeWire audio system configured for realtime/low-latency
- Realtime kernel optimizations
- MK3 boot display helper showing boot progress on left display
- Utility scripts from `scripts/` directory
- Systemd services for auto-start

**Note:** This approach builds the image from scratch using pi-gen, which takes longer (30-60 minutes) but produces an official-style image compatible with Raspberry Pi Imager.

## Requirements

### Host System (for building the image)

- Linux system (x86_64 or ARM64)
- **Docker** (recommended) OR root/sudo access for direct build
- Required packages (the script will check and prompt if missing):
  - `git` - Clone pi-gen repository
  - `docker` - Container runtime (recommended)
  - `qemu-user-static` - ARM64 emulation (for Docker builds)

**Docker Setup (Recommended):**
```bash
# Install Docker
sudo apt-get install docker.io

# Add user to docker group
sudo usermod -aG docker $USER
# Log out and back in for group changes to take effect
```

The script automatically detects your package manager and provides the correct installation command if dependencies are missing.

### Target System

- Raspberry Pi 4 or newer (ARM64)
- SD card (8GB minimum, 16GB+ recommended)
- MK3 controller (optional, for boot display)

## Usage

### Basic Usage

```bash
cd pi-tools
sudo ./package-for-pi.sh
```

This will:
1. Clone or update the pi-gen repository (official Raspberry Pi OS image builder)
2. Create a custom stage (`stage6-maschinepi`) that:
   - Installs PipeWire and audio dependencies
   - Builds maschinepi from source (or installs pre-built binary)
   - Builds and installs mk3-boot-display helper
   - Configures realtime audio optimizations
   - Sets up systemd services
3. Configure pi-gen to build a Lite image (skips desktop stages)
4. Build the complete image using pi-gen (30-60 minutes)
5. Copy the final image to `pi-tools/output/`

**Note:** The build process takes 30-60 minutes as it builds the entire Raspberry Pi OS from scratch. This produces an official-style image that's fully compatible with Raspberry Pi Imager.

### Options

- `--output-dir DIR` - Specify output directory (default: `pi-tools/output`)
- `--work-dir DIR` - Specify work directory for pi-gen (default: `pi-tools/work`)
- `--pi-gen-dir DIR` - Specify pi-gen directory (default: `pi-tools/work/pi-gen`)
- `--no-auto-start` - Disable auto-start of maschinepi service
- `--skip-build` - Skip building maschinepi (use pre-built binary via `MASCHINEPI_BINARY` env var)
- `--compress` - Compress final image with xz (pi-gen already compresses by default)
- `--dry-run` - Show what would be done without making changes

### Example

```bash
# Build image with Docker (recommended)
./package-for-pi.sh

# Build with custom output directory
./package-for-pi.sh --output-dir ~/images

# Skip building maschinepi and use pre-built binary
export MASCHINEPI_BINARY=/path/to/maschinepi
./package-for-pi.sh --skip-build

# Disable auto-start of maschinepi service
./package-for-pi.sh --no-auto-start
```

### Using Pre-built Binary

If you want to skip the build step and use a pre-built maschinepi binary:

```bash
export MASCHINEPI_BINARY=/path/to/your/maschinepi
./package-for-pi.sh --skip-build
```

### How pi-gen Works

The script uses the official [pi-gen](https://github.com/RPI-Distro/pi-gen) tool to build Raspberry Pi OS images from scratch:

1. **Clones pi-gen** (arm64 branch) into the work directory
2. **Creates custom stage** (`stage6-maschinepi`) that:
   - Runs after the base Lite image (stage2)
   - Installs packages, builds maschinepi, configures system
3. **Configures pi-gen** to build a Lite image (skips desktop stages 3-5)
4. **Builds the image** using Docker (recommended) or directly with root
5. **Outputs** a flashable `.img` or `.img.xz` file

This approach ensures the image is built using the same process as official Raspberry Pi OS images, guaranteeing compatibility with Raspberry Pi Imager.

## Output

The script creates a flashable `.img` file in the output directory:

```
maschinepi-raspios-YYYYMMDD.img
```

If `--compress` is used, the file will be:

```
maschinepi-raspios-YYYYMMDD.img.xz
```

## Flashing to SD Card

### Using dd (Linux/macOS)

```bash
# Uncompress if needed
xz -d maschinepi-raspios-YYYYMMDD.img.xz

# Flash to SD card (replace /dev/sdX with your SD card device)
sudo dd if=maschinepi-raspios-YYYYMMDD.img of=/dev/sdX bs=4M status=progress
sync
```

### Using Raspberry Pi Imager

1. Download and install [Raspberry Pi Imager](https://www.raspberrypi.com/software/)
2. Choose "Use custom image"
3. Select the `.img` file created by the packaging script
4. Select your SD card and write

**Note:** The image is in standard Raspberry Pi OS format and is fully compatible with Pi Imager.

## Image Contents

### Installed Software

- **maschinepi** - Main DAW application (`/usr/local/bin/maschinepi`)
- **mk3-boot-display** - Boot progress display helper (`/usr/local/bin/mk3-boot-display`)
- **Utility scripts** - From `scripts/` directory (`/opt/maschinepi/scripts/`)

### System Configuration

- **PipeWire** - Configured for realtime audio with low latency (48kHz, 64 sample buffer)
- **Kernel parameters** - Optimized for realtime audio (swappiness, dirty ratios, RT scheduling)
- **CPU governor** - Set to performance mode
- **udev rules** - MK3 controller USB device access (`/etc/udev/rules.d/99-mk3-controller.rules`)

### Systemd Services

- **mk3-boot-display.service** - Shows boot progress on MK3 left display
- **maschinepi.service** - Auto-starts maschinepi on boot (if `--auto-start` enabled)

## Boot Display

The MK3 boot display helper shows boot progress on the left display (480x272):

- Boot phase status
- Progress bar
- System status messages
- Elapsed boot time
- MaschinePI service status

The service starts early in the boot process and continues running to show system status.

## Realtime Audio Configuration

The image is optimized for realtime audio/DAW work:

- **PipeWire** configured with:
  - 48kHz sample rate
  - 64 sample buffer (1.33ms latency)
  - Realtime priority scheduling
- **Kernel parameters**:
  - Reduced swappiness (10)
  - Lower dirty ratios for faster I/O
  - Realtime scheduling enabled
- **CPU governor** set to performance mode
- **Audio group** permissions for USB device access

## Troubleshooting

### Missing Dependencies

The script automatically checks for all required dependencies:
- `git` - For cloning pi-gen
- `docker` - For containerized builds (recommended)
- `qemu-user-static` - For ARM64 emulation in Docker

If dependencies are missing, the script will exit with clear installation instructions.

### Docker Not Available

If Docker is not available or not accessible:

1. **Install Docker:**
   ```bash
   sudo apt-get install docker.io
   sudo usermod -aG docker $USER
   # Log out and back in
   ```

2. **Or use direct build (requires root):**
   ```bash
   sudo ./package-for-pi.sh
   ```

### Build Fails in pi-gen Stage

If the maschinepi stage fails:

1. **Check pi-gen logs:**
   - Docker build: Check Docker container logs
   - Direct build: Check `pi-tools/work/pi-gen/work/` directory

2. **Common issues:**
   - Network connectivity (pi-gen downloads packages)
   - Insufficient disk space (pi-gen needs ~10GB+ free)
   - Missing source files (ensure maschinepi source is accessible)

3. **Rebuild specific stage:**
   ```bash
   cd pi-tools/work/pi-gen
   CLEAN=1 CONTINUE=1 ./build-docker.sh
   ```

### Image Not Found After Build

If the script can't find the generated image:

1. Check `pi-tools/work/pi-gen/deploy/` directory
2. Look for files matching `maschinepi*.img` or `maschinepi*.img.xz`
3. pi-gen may have named it differently - check the deploy directory manually

### MK3 device not detected

- Check udev rules: `/etc/udev/rules.d/99-mk3-controller.rules`
- Verify user is in `audio` and `plugdev` groups
- Check USB connection and device permissions

### Audio latency issues

- Verify PipeWire is running: `systemctl status pipewire`
- Check realtime priorities: `chrt -p <pid>`
- Review kernel parameters: `sysctl vm.swappiness vm.dirty_ratio`

## Development

### Modifying the Image Setup

Edit `pi-image-setup.sh` to change what gets installed or configured.

### Adding Dependencies

Add packages to `pi-dependencies.txt` (one per line, comments with `#`).

### Customizing Boot Display

Modify `mk3-boot-display.c` to change what's shown on the boot display.

## Files

- `package-for-pi.sh` - Main packaging script
- `pi-image-setup.sh` - Setup script run inside chroot
- `pi-dependencies.txt` - List of required system packages
- `realtime-config.sh` - Realtime audio optimizations
- `mk3-boot-display.c` - Boot display helper source
- `mk3-boot-display.service` - Systemd service for boot display
- `maschinepi.service` - Systemd service for maschinepi
- `README.md` - This file

## License

See main project LICENSE file.
