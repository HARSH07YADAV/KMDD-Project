#!/bin/bash
# ════════════════════════════════════════════════════════════
#  KMDD — Driver Loader Helper
#  Loads kernel modules and sets permissions for userspace
# ════════════════════════════════════════════════════════════

set -e

# Colors
GREEN='\033[0;32m'
CYAN='\033[0;36m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${CYAN}[KMDD] Loading drivers...${NC}"

# Check for .ko files
if [ ! -f "keyboard_driver.ko" ] || [ ! -f "mouse_driver.ko" ]; then
    echo -e "${RED}Error: Kernel modules not found. Run 'make' first.${NC}"
    exit 1
fi

# Remove if already loaded
if lsmod | grep -q "keyboard_driver"; then
    echo "Removing existing keyboard_driver..."
    sudo rmmod keyboard_driver
fi
if lsmod | grep -q "mouse_driver"; then
    echo "Removing existing mouse_driver..."
    sudo rmmod mouse_driver
fi
if lsmod | grep -q "touchpad_driver"; then
    echo "Removing existing touchpad_driver..."
    sudo rmmod touchpad_driver
fi

# Load modules
echo "Inserting keyboard_driver.ko..."
sudo insmod keyboard_driver.ko

echo "Inserting mouse_driver.ko..."
sudo insmod mouse_driver.ko

if [ -f "touchpad_driver.ko" ]; then
    echo "Inserting touchpad_driver.ko..."
    sudo insmod touchpad_driver.ko
fi

# Permissions
echo "Setting permissions on /dev/input/event*..."
# Give access to input group, add current user to it if needed
# Or just generous permissions for the lab environment
sudo chmod 666 /dev/input/event* 2>/dev/null || true

# Sysfs permissions for injection (lab convenience)
echo "Setting permissions on sysfs injection nodes..."
sudo find /sys/devices/virtual/input -name "inject_*" -exec chmod 666 {} \; 2>/dev/null || true
sudo find /sys/devices/virtual/input -name "repeat_*" -exec chmod 666 {} \; 2>/dev/null || true
sudo find /sys/devices/virtual/input -name "dpi_*" -exec chmod 666 {} \; 2>/dev/null || true
sudo find /sys/devices/virtual/input -name "led_*" -exec chmod 444 {} \; 2>/dev/null || true
sudo find /sys/devices/virtual/input -name "log_level" -exec chmod 666 {} \; 2>/dev/null || true

echo -e "${GREEN}✓ Drivers loaded and configured.${NC}"
echo -e "You can now run the backend: ${CYAN}python3 backend/server.py${NC}"
