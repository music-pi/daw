#!/bin/bash
# MaschinePI Launcher - Checks for boot skip flag and launches appropriate program

FLAG_FILE="/tmp/maschinepi-skip-boot"

if [ -f "$FLAG_FILE" ]; then
    echo "Boot skip flag detected. Launching system-config..."
    rm -f "$FLAG_FILE"
    exec /usr/local/bin/maschinepi-system-config
else
    echo "Normal boot. Launching MaschinePI DAW..."
    exec /usr/local/bin/maschinepi
fi

