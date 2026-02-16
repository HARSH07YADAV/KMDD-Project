# Linux Keyboard & Mouse Driver Project

**Educational Operating Systems Project - Virtual Input Device Drivers**

## Project Overview

This project implements two Linux kernel modules that demonstrate device driver development:

- **keyboard_driver.ko**: PS/2-style keyboard driver with scan code translation
- **mouse_driver.ko**: PS/2-style mouse driver with motion and button tracking

Both drivers integrate with the Linux input subsystem, making them compatible with standard user-space tools like `evtest` and graphical environments.

## Architecture

```text
Hardware Layer (Simulated)
        |
        v
   IRQ Handler / Work Queue
        |
        v
Scan Code/Packet Parser
        |
        v
  Linux Input Subsystem
        |
        v
   /dev/input/eventX
        |
        v
  User-space Applications
```

### Data Flow

1. **Hardware Simulation**: Since we may not have direct PS/2 hardware, drivers expose sysfs interfaces to inject simulated scan codes/packets
2. **IRQ Handler**: Processes input data, handles buffering with proper locking (spinlock for IRQ context)
3. **Translation Layer**:
   - Keyboard: Scan code → Linux keycode → input event
   - Mouse: PS/2 3-byte packets → relative motion + button events
4. **Input Subsystem**: Linux `input_dev` framework reports events to user space
5. **User Applications**: Standard tools (`evtest`, X11, Wayland) consume events via `/dev/input/eventX`

## Requirements

- Linux kernel 5.x or 6.x (tested on 5.15+, 6.x)
- Kernel headers installed: `sudo apt install linux-headers-$(uname -r)`
- GCC and Make
- Root/sudo access for module insertion
- Optional: QEMU for virtual testing

## Build Instructions

### Building the Kernel Modules

```bash
# Build both drivers
make

# Build individual modules
make keyboard
make mouse

# Clean build artifacts
make clean
```

The Makefile uses the kernel build system (`kbuild`) and compiles against your running kernel.

### Building User-space Tools

```bash
# Build the event reader utility
make userspace

# Or manually:
gcc -o userspace/reader userspace/reader.c
```

## Installation and Usage

### Loading the Modules

```bash
# Load keyboard driver
sudo insmod drivers/keyboard_driver.ko

# Load mouse driver
sudo insmod drivers/mouse_driver.ko

# Verify loaded
lsmod | grep -E 'keyboard_driver|mouse_driver'

# Check kernel messages
dmesg | tail -20
```

### Using the Install Script

```bash
# Automated installation
sudo bash install.sh

# This will:
# - Insert both modules
# - Find the input event devices
# - Create convenient symlinks
# - Display usage instructions
```

### Finding Your Device

```bash
# List input devices
cat /proc/bus/input/devices | grep -A 5 "Virtual"

# Find event number
ls -l /dev/input/by-id/ | grep Virtual

# Or check dmesg output
dmesg | grep "registered as"
```

### Reading Events

```bash
# Use the provided reader tool
sudo ./userspace/reader /dev/input/event<N>

# Use evtest (if installed)
sudo evtest /dev/input/event<N>

# Simple raw view (binary output)
sudo hexdump -C /dev/input/event<N>
```

## Testing

### Simulating Keyboard Input

The keyboard driver exposes a sysfs interface to inject scan codes:

```bash
# Find the sysfs path
KBDSYSFS=/sys/devices/virtual/input/input*/inject_scancode

# Inject scan code for 'A' key press (0x1E)
echo 0x1E | sudo tee $KBDSYSFS

# Inject release (scan code | 0x80)
echo 0x9E | sudo tee $KBDSYSFS
```

### Simulating Mouse Input

```bash
# Find the sysfs path
MOUSESYSFS=/sys/devices/virtual/input/input*/inject_packet

# Inject mouse packet: buttons dx dy (3 bytes, space-separated)
# Example: left button pressed, move right 10, down 5
echo "0x01 0x0A 0x05" | sudo tee $MOUSESYSFS

# No buttons, move left 5, up 3
echo "0x00 0xFB 0xFD" | sudo tee $MOUSESYSFS
```

### Running Test Scripts

```bash
# Automated keyboard test
sudo bash tests/test_keyboard.sh

# Automated mouse test
sudo bash tests/test_mouse.sh

# Watch events while testing
sudo ./userspace/reader /dev/input/event<N> &
sudo bash tests/test_keyboard.sh
```

## QEMU Testing

### Preparing a QEMU Environment

```bash
# Install QEMU
sudo apt install qemu-system-x86 qemu-utils

# Download a minimal Linux image (example: Debian cloud image)
wget https://cloud.debian.org/images/cloud/bullseye/latest/debian-11-generic-amd64.qcow2

# Or build your own minimal kernel + initramfs
```

### Running in QEMU

```bash
# Basic QEMU command with keyboard/mouse emulation
qemu-system-x86_64 \
  -kernel /boot/vmlinuz-$(uname -r) \
  -initrd /boot/initrd.img-$(uname -r) \
  -m 2G \
  -smp 2 \
  -append "console=ttyS0 root=/dev/sda1" \
  -drive file=debian-11-generic-amd64.qcow2,format=qcow2 \
  -device usb-ehci,id=usb \
  -device usb-kbd \
  -device usb-mouse \
  -nographic

# Alternative: Use VNC for graphical testing
qemu-system-x86_64 \
  -kernel /boot/vmlinuz-$(uname -r) \
  -m 2G \
  -vnc :1 \
  -hda debian-11-generic-amd64.qcow2
```

### Testing Inside QEMU

1. Boot the VM
2. Copy driver files into VM (use scp or shared folder):

   ```bash
   # On host, create shared folder
   qemu-system-x86_64 ... -virtfs local,path=/home/yadavji/kernel-driver-project,mount_tag=hostshare,security_model=passthrough
   
   # In guest
   mkdir /mnt/host
   mount -t 9p -o trans=virtio hostshare /mnt/host
   ```

3. Build modules inside VM (ensure kernel headers match)
4. Load and test as described above

### Debugging in QEMU

```bash
# Enable debug output
echo 8 | sudo tee /proc/sys/kernel/printk

# Monitor kernel messages
dmesg -w

# Use QEMU monitor
# Press Ctrl+Alt+2 to access monitor, Ctrl+Alt+1 to return
```

## Unloading Modules

```bash
# Remove modules
sudo rmmod mouse_driver
sudo rmmod keyboard_driver

# Verify
lsmod | grep -E 'keyboard_driver|mouse_driver'
```

## Web Dashboard

### Setup

```bash
# Install Python dependencies
pip3 install websockets

# Or use the Makefile
make deps
```

### Starting the Dashboard

```bash
# Simulation mode (no kernel modules needed — great for demos)
make dashboard-sim
# Or: python3 backend/server.py --simulate

# Live mode (requires loaded kernel modules, run with sudo)
sudo make dashboard
# Or: sudo python3 backend/server.py

# Custom ports
python3 backend/server.py --port 9090 --ws-port 9091 --simulate
```

Then open **<http://localhost:8080>** in your browser.

### Dashboard Features

- **Live Events** — Real-time scrolling feed of keyboard, mouse, and touchpad events
- **Virtual Keyboard** — Interactive on-screen keyboard that highlights keys as events arrive; click keys to inject scan codes
- **Mouse Tracker** — Canvas-based cursor trail visualization with button state indicators
- **Statistics** — Key frequency tracking, events per second, and counters
- **Settings** — WebSocket connection and display configuration

## Code Structure

```text
kernel-driver-project/
├── README.md             # KMDD — Linux Kernel Mouse & Keyboard Driver

A comprehensive Linux kernel driver project implementing virtual input devices (keyboard, mouse, touchpad) with a feature-rich, real-time web dashboard for visualization and control.

![Dashboard Preview](docs/dashboard_preview.png)

## Overview

This project demonstrates advanced Linux kernel programming and userspace integration. It consists of three main layers:

1.  **Kernel Modules:**
    *   `vkbd_driver`: Virtual keyboard with sysfs configuration (repeat rate, LEDs).
    *   `vmouse_driver`: Virtual mouse with absolute/relative positioning and DPI scaling.
    *   `vtouch_driver`: Virtual touchpad with multi-touch gesture support.

2.  **Userspace Daemon:**
    *   `backend/server.py`: Python asyncio server that bridges kernel events to the web UI via WebSockets and handles injection commands.
    *   `userspace/event_logger`: C daemon that reads `/dev/input/eventX` and logs interactions.

3.  **Web Dashboard (UI):**
    *   **Live Event Feed:** Real-time stream of input events with syntax highlighting.
    *   **Visualizers:** Interactive virtual keyboard, mouse motion tracker, and touchpad gesture canvas.
    *   **Statistics:** Click distribution pie charts, heatmaps, distance tracking, and EPS (Events Per Second) graphs.
    *   **Injection Control:** Send custom input patterns (text-to-scancode, mouse paths, presets) to the kernel.
    *   **Advanced Settings:** Configure driver parameters (DPI, Repeat Rate, LEDs) on the fly.

## Project Structure

```text
.
├── kernel/             # Kernel modules (C)
│   ├── keyboard_driver.c
│   ├── mouse_driver.c
│   └── touchpad_driver.c
├── userspace/          # Userspace tools (C)
│   ├── reader.c        # Event device reader
│   └── event_logger.c  # JSON logging daemon
├── backend/            # Web server (Python)
│   └── server.py       # WebSocket + Sysfs bridge
├── ui/                 # Web Dashboard (HTML/JS/CSS)
│   ├── index.html
│   ├── app.js
│   └── styles.css
├── tests/              # Test suites
│   ├── test_e2e.sh     # Full stack E2E verification
│   ├── test_performance.sh
│   └── test_*.sh       # Driver-specific script tests
└── docs/               # Documentation
    ├── roadmap.md
    ├── lab_report.md
    └── presentation.md
```

### Educational Operating Systems Project

## Detailed Setup & Usage

### 1. Build Kernel Modules

```bash
make clean
make all
```

### 2. Load Modules

Use the helper script to load drivers and set permissions:

```bash
sudo ./load_drivers.sh
```

### 3. Start the Dashboard

Run the backend server (requires Python 3.7+ and `websockets`):

```bash
# Production mode (requires loaded modules)
sudo python3 backend/server.py

# Simulation mode (UI dev/testing without kernel modules)
python3 backend/server.py --simulate
```

Open **<http://localhost:8080>** in your web browser.

## Features

### 🖥️ Web Dashboard

- **Injection Tab:** Type text to auto-inject scan codes, or draw a path on the canvas to simulate mouse movements.

- **Stats Tab:** Monitor driver performance with active uptime, key density heatmaps, and movement totals.
- **Settings Tab:** Adjust kernel parameters like `repeat_rate` or `dpi_multiplier` instantly via sysfs.

### 🐧 Kernel Drivers

- **Sysfs Interfaces:**
  - `/sys/devices/virtual/input/inputX/repeat_rate_ms`
  - `/sys/devices/virtual/input/inputX/dpi_multiplier`
  - `/sys/devices/virtual/input/inputX/led_caps` (Read-only status)

- **Procfs Statistics:** `/proc/vkbd_stats` tracks detailed usage metrics.

## Verification & Testing

### Automated Test Suite

Run the full end-to-end test to verify the stack:

```bash
# Runs backend in sim mode, checks HTTP/WebSocket, and verifies injection
./tests/test_e2e.sh
```

Run the performance stress test:

```bash
# Injects 1000+ events/sec to verify throughput
./tests/test_performance.sh
```

### Manual Driver Testing

```bash
# Test keyboard features (LEDs, repetition)
sudo ./tests/test_keyboard.sh

# Test mouse movement and scaling
sudo ./tests/test_mouse.sh
```

## License

MIT License. Created for OS Course Project 2025.
