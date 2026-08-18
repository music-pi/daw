#!/usr/bin/env bash
set -euo pipefail

# Realtime audio configuration for Raspberry Pi
# This script configures the system for low-latency audio/DAW work

CONFIG_DIR="${1:-/etc}"

echo "==> Configuring realtime audio optimizations..."

# =================== Kernel parameters ===================
cat > "${CONFIG_DIR}/sysctl.d/99-realtime-audio.conf" << 'EOF'
# Realtime audio optimizations
vm.swappiness=10
vm.dirty_ratio=3
vm.dirty_background_ratio=1
kernel.sched_rt_runtime_us=-1
EOF

# =================== CPU governor ========================
# Set CPU governor to performance mode (disable power saving)
cat > "${CONFIG_DIR}/systemd/system/cpu-performance.service" << 'EOF'
[Unit]
Description=Set CPU governor to performance
After=multi-user.target

[Service]
Type=oneshot
ExecStart=/bin/bash -c 'echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor'
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

# =================== PipeWire realtime config ===========
mkdir -p "${CONFIG_DIR}/pipewire/pipewire.conf.d"
cat > "${CONFIG_DIR}/pipewire/pipewire.conf.d/99-realtime.conf" << 'EOF'
context.properties = {
    default.clock.rate = 48000
    default.clock.quantum = 64
    default.clock.min-quantum = 64
    default.clock.max-quantum = 1024
    core.daemon = true
    core.name = "pipewire-0"
}

context.spa-libs = {
    audio.convert.* = audioconvert/libspa-audioconvert
    support.* = support/libspa-support
}

context.modules = [
    { name = libpipewire-module-rtkit
        args = {
            nice.level   = -11
            rt.prio      = 88
            rt.time.soft = 2000000
            rt.time.hard = 2000000
        }
        flags = [ ifexists nofail ]
    }
]
EOF

# =================== Audio group and permissions ========
# Ensure audio group exists and has proper permissions
groupadd -f audio || true
groupadd -f pipewire || true

# =================== udev rules for MK3 =================
mkdir -p "${CONFIG_DIR}/udev/rules.d"
cat > "${CONFIG_DIR}/udev/rules.d/99-mk3-controller.rules" << 'EOF'
# MK3 Controller USB device rules
SUBSYSTEM=="usb", ATTRS{idVendor}=="17cc", ATTRS{idProduct}=="1600", MODE="0664", GROUP="audio"
EOF

# =================== Enable services ====================
if command -v systemctl >/dev/null 2>&1; then
    systemctl enable cpu-performance.service || true
fi

echo "==> Realtime audio configuration complete"

