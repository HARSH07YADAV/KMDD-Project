# Quick Start Guide

Get the drivers running in under 5 minutes!

## Prerequisites Check

```bash
# Check if kernel headers are installed
ls /lib/modules/$(uname -r)/build

# If not found, install them:
sudo apt install linux-headers-$(uname -r)  # Ubuntu/Debian
# sudo dnf install kernel-devel              # Fedora
# sudo yum install kernel-devel              # CentOS/RHEL
```

## Build and Test (3 Commands)

```bash
# 1. Build everything
make

# 2. Load the drivers
sudo bash install.sh

# 3. Run a test
sudo bash tests/test_keyboard.sh
```

That's it! You should see events being generated.

## View Events in Real-Time

```bash
# Find your event device
cat /proc/bus/input/devices | grep -A 5 "Virtual PS/2"

# Read events (replace X with your event number)
sudo ./userspace/reader /dev/input/eventX
```

## Quick Manual Test

```bash
# Test keyboard - type 'HELLO'
echo 0x23 | sudo tee /sys/devices/virtual/input/input*/inject_scancode  # H press
echo 0xA3 | sudo tee /sys/devices/virtual/input/input*/inject_scancode  # H release
echo 0x12 | sudo tee /sys/devices/virtual/input/input*/inject_scancode  # E press
echo 0x92 | sudo tee /sys/devices/virtual/input/input*/inject_scancode  # E release

# Test mouse - move cursor
echo "0x08 0x14 0x14" | sudo tee /sys/devices/virtual/input/input*/inject_packet
```

## Cleanup

```bash
# Unload modules
sudo rmmod mouse_driver keyboard_driver

# Or just reboot (modules don't persist)
```

## Troubleshooting

**"Cannot find module"** → Run `make` first  
**"Permission denied"** → Use `sudo`  
**"No such file"** → Module not loaded, run `sudo bash install.sh`  
**Module won't load** → Run `make clean && make` to rebuild

## Common Scan Codes

| Key | Press | Release |
|-----|-------|---------|
| A   | 0x1E  | 0x9E    |
| S   | 0x1F  | 0x9F    |
| D   | 0x20  | 0xA0    |
| Space | 0x39 | 0xB9   |
| Enter | 0x1C | 0x9C   |
| Shift | 0x2A | 0xAA   |

## Mouse Packet Examples

| Action | Packet |
|--------|--------|
| Left click | `0x09 0x00 0x00` |
| Right click | `0x0A 0x00 0x00` |
| Move right 10px | `0x08 0x0A 0x00` |
| Move down 10px | `0x08 0x00 0x0A` |
| Left drag right | `0x09 0x0A 0x00` |

## Next Steps

See README.md for complete documentation, QEMU testing, and detailed explanations.
