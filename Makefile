# Makefile for Virtual Keyboard and Mouse Drivers (v2.0)
# Educational OS Project

# Kernel module names
obj-m += keyboard_driver.o
obj-m += mouse_driver.o
obj-m += touchpad_driver.o

# Kernel build directory (current running kernel)
KDIR := /lib/modules/$(shell uname -r)/build

# Current directory
PWD := $(shell pwd)

# User-space tools
USERSPACE_READER := userspace/reader
USERSPACE_LOGGER := userspace/event_logger

# Compiler flags for userspace
CFLAGS := -Wall -Wextra -O2

# Default target: build everything
all: modules userspace

# Build kernel modules
modules:
	@echo "════════════════════════════════════════"
	@echo "  Building kernel modules..."
	@echo "════════════════════════════════════════"
	$(MAKE) -C $(KDIR) M=$(PWD)/drivers modules
	@echo ""
	@echo "✓ Kernel modules built successfully!"
	@ls -lh drivers/*.ko

# Build individual modules
keyboard:
	@echo "Building keyboard driver..."
	$(MAKE) -C $(KDIR) M=$(PWD)/drivers keyboard_driver.ko

mouse:
	@echo "Building mouse driver..."
	$(MAKE) -C $(KDIR) M=$(PWD)/drivers mouse_driver.ko

# Build user-space tools
userspace:
	@echo "════════════════════════════════════════"
	@echo "  Building user-space tools..."
	@echo "════════════════════════════════════════"
	gcc $(CFLAGS) -o $(USERSPACE_READER) userspace/reader.c
	gcc $(CFLAGS) -o $(USERSPACE_LOGGER) userspace/event_logger.c -ljson-c 2>/dev/null || \
		gcc $(CFLAGS) -o $(USERSPACE_LOGGER) userspace/event_logger.c -DNO_JSONC
	@echo "✓ User-space tools built successfully!"
	@ls -lh $(USERSPACE_READER) $(USERSPACE_LOGGER)

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	$(MAKE) -C $(KDIR) M=$(PWD)/drivers clean
	rm -f $(USERSPACE_READER) $(USERSPACE_LOGGER)
	rm -f drivers/*.o drivers/*.ko drivers/*.mod* drivers/.*.cmd drivers/Module.symvers
	rm -rf drivers/.tmp_versions
	@echo "✓ Clean complete!"

# Install modules (requires root)
install:
	@echo "════════════════════════════════════════"
	@echo "  Installing modules (requires root)..."
	@echo "════════════════════════════════════════"
	sudo insmod drivers/keyboard_driver.ko
	sudo insmod drivers/mouse_driver.ko
	-sudo insmod drivers/touchpad_driver.ko
	@echo "✓ Modules installed. Check dmesg for details."
	@echo ""
	@dmesg | tail -20

# Uninstall modules (requires root)
uninstall:
	@echo "Uninstalling modules..."
	-sudo rmmod touchpad_driver
	-sudo rmmod mouse_driver
	-sudo rmmod keyboard_driver
	@echo "✓ Modules uninstalled."

# Show module info
info:
	@echo "═══ Keyboard Driver Info ═══"
	@modinfo drivers/keyboard_driver.ko 2>/dev/null || echo "Not built yet"
	@echo ""
	@echo "═══ Mouse Driver Info ═══"
	@modinfo drivers/mouse_driver.ko 2>/dev/null || echo "Not built yet"

# Check if modules are loaded + show stats
status:
	@echo "═══ Module Status ═══"
	@lsmod | grep -E "keyboard_driver|mouse_driver" || echo "No modules loaded"
	@echo ""
	@echo "═══ Input Devices ═══"
	@cat /proc/bus/input/devices | grep -A 5 "Virtual" || echo "No virtual devices found"
	@echo ""
	@echo "═══ Driver Statistics ═══"
	@cat /proc/vkbd_stats 2>/dev/null || echo "/proc/vkbd_stats not available"
	@echo ""
	@cat /proc/vmouse_stats 2>/dev/null || echo "/proc/vmouse_stats not available"

# Run keyboard tests
test-keyboard:
	@echo "Running keyboard tests..."
	sudo bash tests/test_keyboard.sh

# Run mouse tests
test-mouse:
	@echo "Running mouse tests..."
	sudo bash tests/test_mouse.sh

# Run touchpad tests
test-touchpad:
	@echo "Running touchpad tests..."
	sudo bash tests/test_touchpad.sh

# Run unit tests
test-unit:
	@echo "Running unit tests..."
	bash tests/test_unit.sh

# Run all tests
test: test-keyboard test-mouse test-touchpad test-unit

# Install Python dependencies for dashboard
deps:
	@echo "Installing Python dependencies..."
	pip3 install websockets 2>/dev/null || pip3 install --break-system-packages websockets
	@echo "✓ Dependencies installed!"

# Start web dashboard (live mode)
dashboard: deps
	@echo "════════════════════════════════════════"
	@echo "  Starting KMDD Web Dashboard..."
	@echo "════════════════════════════════════════"
	python3 backend/server.py

# Start web dashboard (simulation mode)
dashboard-sim: deps
	@echo "════════════════════════════════════════"
	@echo "  Starting KMDD Web Dashboard (Simulation)..."
	@echo "════════════════════════════════════════"
	python3 backend/server.py --simulate

# Help target
help:
	@echo ""
	@echo "╔══════════════════════════════════════════╗"
	@echo "║  Virtual Input Driver Project v2.0       ║"
	@echo "╠══════════════════════════════════════════╣"
	@echo "║  Build:                                  ║"
	@echo "║    make              Build everything     ║"
	@echo "║    make modules      Build kernel modules ║"
	@echo "║    make keyboard     Build keyboard only  ║"
	@echo "║    make mouse        Build mouse only     ║"
	@echo "║    make userspace    Build reader tool     ║"
	@echo "║    make clean        Remove artifacts     ║"
	@echo "║                                          ║"
	@echo "║  Install:                                ║"
	@echo "║    make install      Load modules         ║"
	@echo "║    make uninstall    Unload modules       ║"
	@echo "║                                          ║"
	@echo "║  Test:                                   ║"
	@echo "║    make test         Run all tests        ║"
	@echo "║    make test-keyboard  Keyboard tests     ║"
	@echo "║    make test-mouse     Mouse tests        ║"
	@echo "║                                          ║"
	@echo "║  Info:                                   ║"
	@echo "║    make info         Module information   ║"
	@echo "║    make status       Module + stats       ║"
	@echo "║    make help         This help message    ║"
	@echo "╚══════════════════════════════════════════╝"

.PHONY: all modules keyboard mouse userspace clean install uninstall info status test test-keyboard test-mouse help deps dashboard dashboard-sim
