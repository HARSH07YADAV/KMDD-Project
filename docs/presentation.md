# Virtual Input Device Drivers

## Educational Operating Systems Project

**Presentation Outline (7 Slides)**

---

## Slide 1: Title & Overview

**Virtual PS/2 Keyboard & Mouse Drivers**  
*Linux Kernel Module Development*

**Project Components:**

- Two loadable kernel modules (keyboard_driver.ko, mouse_driver.ko)
- Linux input subsystem integration
- Hardware-free testing via sysfs
- User-space event reader and automated tests

**Learning Goals:**

- Understand device driver architecture
- Master interrupt handling (top-half/bottom-half)
- Implement kernel synchronization primitives
- Work with real kernel APIs

---

## Slide 2: Architecture & Data Flow

**Layered Driver Architecture**

```
User Applications (evtest, X11, reader)
        ↓
/dev/input/eventX (input subsystem)
        ↓
Translation Layer (scan codes → events)
        ↓
Bottom Half (tasklet processing)
        ↓
Circular Buffer (spinlock-protected)
        ↓
IRQ Simulation (sysfs injection)
```

**Key Design Decisions:**

- Input subsystem: Standard interface, automatic event queuing
- Tasklet bottom-half: Defers heavy processing out of IRQ context
- Circular buffers: Handle burst input, prevent data loss
- Sysfs testing: No hardware required, fully deterministic

---

## Slide 3: Keyboard Driver Implementation

**PS/2 Scan Code Translation**

**Process Flow:**

1. **Input**: User writes scan code to sysfs (e.g., `0x1E` for 'A')
2. **Buffering**: Scan code added to circular buffer (128 bytes)
3. **Tasklet**: Scheduled to process buffered codes
4. **Translation**: Scan code → Linux keycode (50+ keys mapped)
5. **Detection**: Bit 7 indicates release (make/break codes)
6. **Reporting**: `input_report_key()` + `input_sync()` to subsystem
7. **Output**: Event appears in `/dev/input/eventX`

**Features:**

- Handles press/release for all standard keys
- Tracks modifier state (shift, ctrl, alt)
- Proper spinlock protection for concurrent access
- Debug messages via printk/dmesg

---

## Slide 4: Mouse Driver Implementation

**PS/2 Packet Parsing**

**3-Byte Packet Format:**

- **Byte 0**: Status byte (buttons | overflow | sign | always-1)
- **Byte 1**: X movement (8-bit signed, -127 to +127)
- **Byte 2**: Y movement (8-bit signed, inverted for Linux)

**Processing Pipeline:**

1. **Input**: Three hex values via sysfs (e.g., `0x09 0x10 0xF0`)
2. **Assembly**: Bytes collected until complete 3-byte packet
3. **Validation**: Check bit 3 (always 1 in valid packets)
4. **Extraction**: Parse button states and motion values
5. **Reporting**: Buttons via `input_report_key()`, motion via `input_report_rel()`
6. **Output**: Events for button clicks and cursor movement

**Challenges Addressed:**

- Packet synchronization (prevent misalignment)
- Y-axis convention differences (PS/2 vs Linux)
- Signed arithmetic for negative movement

---

## Slide 5: Web Dashboard & Advanced Features

**Real-Time Visualization & Control**

**Architecture:**

- **Frontend**: HTML5/JS Single Page App + Glassmorphism CSS
- **Backend**: Python Asyncio WebSocket Server
- **Kernel**: Standard Sysfs & Input Subsystem Integration

**Key Capabilities:**

- **Live Feed**: Scrolling log of all input events
- **Visualizers**: Virtual keyboard, mouse trails, touchpad canvas
- **Injection Control**:
  - Text-to-Scancode converter
  - Path drawing on canvas
  - Preset patterns (Button barrage, Circle motion)
- **Dynamic Tuning**: Adjust DPI, Repeat Rate, LEDs on the fly

---

## Slide 6: Testing & Demonstration

**Comprehensive Testing Strategy**

**1. Build & Install:**

```bash
make                    # Build modules
sudo bash install.sh    # Load modules
```

**2. Manual Testing:**

```bash
# Keyboard: Inject 'A' key press/release
echo 0x1E | sudo tee /sys/.../inject_scancode
echo 0x9E | sudo tee /sys/.../inject_scancode

# Mouse: Move right 16px, down 16px, left button
echo "0x09 0x10 0x10" | sudo tee /sys/.../inject_packet
```

**3. Automated Test Scripts:**

- `test_keyboard.sh`: Letters, numbers, modifiers, function keys
- `test_mouse.sh`: Buttons, movement, dragging, circular motion

**4. Event Monitoring:**

```bash
sudo ./userspace/reader /dev/input/eventX
# Or: sudo evtest /dev/input/eventX
```

**Verification:**

- Check dmesg for driver messages
- Confirm events in user-space reader
- No kernel panics, memory leaks, or race conditions

---

## Slide 7: Key Concepts & Takeaways

**Operating Systems Concepts Demonstrated:**

**1. Interrupt Handling**

- Top half: Minimal work, schedule tasklet
- Bottom half: Heavy processing in safer context
- Critical for system responsiveness

**2. Kernel Synchronization**

- Spinlocks in atomic context (IRQ, tasklet)
- `spin_lock_irqsave()` prevents deadlocks
- Protects shared circular buffers

**3. Kernel-User Communication**

- Sysfs: Simple write interface for testing
- Input subsystem: Standard event delivery
- Character devices automatically created

**4. Resource Management**

- Proper init/exit sequences
- Memory allocation/deallocation
- Device registration/unregistration

**Real-World Applications:**

- Same architecture used in production drivers (atkbd, psmouse)
- Minimal changes needed for real hardware (request_irq instead of simulation)
- Patterns apply to USB HID, touchscreen, gamepad drivers

**Project Success Metrics:**
✓ Compiles against kernel 5.x/6.x  
✓ Loads/unloads cleanly (no memory leaks)  
✓ Generates correct events (verified with evtest)  
✓ Handles edge cases (overflow, rapid input)  
✓ Well-commented, educational code  

---

## Demo Notes

**Live Demonstration Flow:**

1. **Show code structure** (drivers/, userspace/, tests/)
2. **Build modules** (`make`)
3. **Load drivers** (`sudo bash install.sh`)
4. **Start event reader** in one terminal
5. **Run test scripts** in another terminal
6. **Show events appearing** in real-time
7. **Check dmesg** for driver debug messages
8. **Unload modules** (`sudo rmmod`)

**Questions to Address:**

- Why use input subsystem vs custom char device?
- When to use spinlocks vs mutexes?
- How does circular buffer prevent data loss?
- Could this work with real PS/2 hardware? (Yes, minimal changes)

---

**Conclusion:**
This project bridges theory and practice in OS development, providing hands-on experience with kernel programming while demonstrating principles applicable to real-world driver development.
