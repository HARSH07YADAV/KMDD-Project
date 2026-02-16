# Virtual Input Device Drivers: Lab Report

## Educational Operating Systems Project

---

## Executive Summary

This project implements two Linux kernel modules that emulate PS/2-style keyboard and mouse hardware interfaces. The drivers demonstrate fundamental operating system concepts including interrupt handling, device driver architecture, kernel-user space communication, and integration with the Linux input subsystem. Both drivers are fully functional, testable without physical hardware, and provide educational insight into low-level device management in modern operating systems.

## 1. Introduction and Motivation

Device drivers form the critical bridge between hardware and software in operating systems. Understanding their design and implementation is essential for systems programmers. This project focuses on input devices—keyboards and mice—which represent classic examples of interrupt-driven I/O devices.

The primary objectives were to:

- Demonstrate kernel module development and lifecycle management
- Implement interrupt handling using Linux tasklets for bottom-half processing
- Integrate with the Linux input subsystem for standardized event delivery
- Provide a testing framework that works without physical PS/2 hardware
- Illustrate proper synchronization using spinlocks in interrupt context
- Create educational code that balances simplicity with real-world patterns

## 2. Architecture and Design

### 2.1 Overall System Architecture

The driver architecture follows a layered approach:

```
┌─────────────────────────────────────┐
│      User-space Applications        │
│    (evtest, X11, Wayland, reader)   │
└─────────────────┬───────────────────┘
                  │ read()
        ┌─────────▼─────────┐
        │  /dev/input/eventX │
        └─────────┬─────────┘
                  │
    ┌─────────────▼─────────────┐
    │  Linux Input Subsystem    │
    │  - Event queue            │
    │  - Device registration    │
    │  - Event routing          │
    └─────────────┬─────────────┘
                  │
        ┌─────────▼─────────┐
        │   Driver Layer    │
        │  - Translation    │
        │  - Event reporting│
        └─────────┬─────────┘
                  │
        ┌─────────▼─────────┐
        │  Bottom Half      │
        │  (Tasklet)        │
        └─────────┬─────────┘
                  │
        ┌─────────▼─────────┐
        │  Circular Buffer  │
        │  (with spinlock)  │
        └─────────┬─────────┘
                  │
        ┌─────────▼─────────┐
        │  IRQ Simulation   │
        │  (via sysfs)      │
        └───────────────────┘
```

### 2.2 Key Components

**Interrupt Handling**: Real PS/2 devices trigger hardware interrupts when data is available. Our implementation simulates this using sysfs-triggered software interrupts. The two-phase interrupt handling model (top half + bottom half) is preserved using Linux tasklets.

**Circular Buffers**: Both drivers maintain circular buffers to queue incoming scan codes or packet bytes. These buffers are protected by spinlocks since they're accessed from both sysfs write context and tasklet context. Buffer size is 128 bytes for keyboard and 256 bytes for mouse (to hold complete 3-byte packets).

**Translation Layer**:

- Keyboard: Converts PS/2 Set 1 scan codes to Linux keycodes, handles make/break codes (press/release detection via bit 7), and tracks modifier key states.
- Mouse: Parses 3-byte PS/2 packets containing button states and relative X/Y motion, validates packet integrity (bit 3 always set), and inverts Y-axis to match Linux convention.

**Linux Input Subsystem Integration**: Instead of implementing custom character devices with read/write/poll operations, both drivers register with the Linux input subsystem. This provides:

- Automatic `/dev/input/eventX` device creation
- Standard event format understood by all userspace tools
- Built-in event queuing and timestamping
- Multi-application access without driver-level management

## 3. Implementation Details

### 3.1 Keyboard Driver

The keyboard driver translates PS/2 scan codes to Linux key events:

1. **Scan Code Injection**: User writes hex value to sysfs → triggers simulated IRQ
2. **Top Half**: Scan code added to circular buffer, tasklet scheduled
3. **Bottom Half (Tasklet)**:
   - Retrieves scan codes from buffer
   - Checks bit 7 for press/release detection
   - Looks up keycode in translation table
   - Reports event via `input_report_key()` and `input_sync()`
4. **Input Subsystem**: Queues event with timestamp, makes available to userspace

The translation table maps 50+ common keys including letters, numbers, function keys, and modifiers. The driver tracks shift key state to demonstrate stateful processing.

### 3.2 Mouse Driver

The mouse driver processes PS/2 3-byte packets:

**Packet Format**:

- Byte 0: Status (buttons | overflow | sign bits | always-1 flag)
- Byte 1: X movement (8-bit signed)
- Byte 2: Y movement (8-bit signed)

**Processing Pipeline**:

1. **Packet Injection**: Three space-separated hex values written to sysfs
2. **Top Half**: Each byte buffered individually, tasklet scheduled
3. **Bottom Half (Tasklet)**:
   - Assembles complete 3-byte packets
   - Validates packet (bit 3 check)
   - Extracts button states (left, right, middle)
   - Extracts signed movement values
   - Inverts Y-axis (PS/2 uses opposite convention)
   - Reports via `input_report_key()` for buttons, `input_report_rel()` for motion
4. **Input Subsystem**: Delivers to userspace

### 3.3 Synchronization

**Spinlocks** are used for buffer access since operations occur in atomic context (tasklet). The pattern used is:

```c
spin_lock_irqsave(&dev->buffer_lock, flags);
// Critical section
spin_unlock_irqrestore(&dev->buffer_lock, flags);
```

This prevents race conditions between sysfs writes (which can occur from any CPU) and tasklet execution.

### 3.4 Testing Infrastructure

**Sysfs Interfaces** provide hardware-free testing:

- Keyboard: `/sys/devices/virtual/input/inputX/inject_scancode`
- Mouse: `/sys/devices/virtual/input/inputX/inject_packet`

**Test Scripts** automate comprehensive driver validation, including edge cases like modifier combinations, rapid inputs, and boundary conditions.

**Event Reader** is a user-space C program that opens `/dev/input/eventX` and displays events in human-readable format with timestamps and color coding.

## 4. Testing and Validation

Testing was performed in three phases:

1. **Unit Testing**: Individual key presses/releases and mouse movements verified correct event generation
2. **Integration Testing**: Combined operations (modifier keys + letters, button + drag) validated state management
3. **Stress Testing**: Rapid injection and buffer overflow scenarios confirmed proper locking and error handling

All tests passed successfully with no kernel panics, memory leaks (verified with `rmmod`), or race conditions detected.

## 5. Results and Discussion

The implementation successfully demonstrates several key OS concepts:

**Interrupt Handling**: The top-half/bottom-half split shows why kernel code must minimize time in interrupt context. Heavy processing (translation, event reporting) occurs in tasklets.

**Kernel Synchronization**: Spinlocks protect shared data (circular buffers) accessed from multiple contexts. The `_irqsave` variant prevents deadlocks by disabling interrupts during critical sections.

**Abstraction Layers**: The input subsystem provides a clean abstraction that simplifies driver code and standardizes the user-kernel interface.

**Resource Management**: Proper cleanup in module exit demonstrates responsible kernel programming—all allocated memory freed, devices unregistered, tasklets killed.

**Design Trade-offs**: Simulation via sysfs trades realism for testability. Real hardware would require request_irq() with actual IRQ numbers, but the core processing logic remains identical.

## 6. Challenges and Solutions

**Challenge 1**: Initial packet synchronization in mouse driver—if the first byte is dropped, subsequent reads misalign.  
**Solution**: Packet validation (bit 3 check) and reset mechanism ensure recovery.

**Challenge 2**: Y-axis inversion confusion between PS/2 and Linux conventions.  
**Solution**: Explicit `dy = -dy` in code with comment explaining the convention difference.

**Challenge 3**: Understanding when to use spinlocks vs. mutexes.  
**Solution**: Rule: atomic context (IRQ, tasklet) requires spinlocks; process context with sleep possibility uses mutexes.

## 7. Web Dashboard Integration

To provide a modern, user-friendly interface for interactively testing the drivers, a real-time web dashboard was developed. This moves beyond simple CLI testing tools to a comprehensive visual environment.

### 7.1 Architecture

The dashboard follows a 3-tier architecture:

1. **Frontend (UI)**: A single-page application (SPA) built with HTML5, CSS3 (Glassmorphism design), and Vanilla JavaScript. It communicates with the backend via WebSockets.
2. **Backend (Server)**: A Python asyncio server using the `websockets` library. It reads from `/dev/input/eventX` for reading events and writes to `/sys/class/input/...` for injection and configuration.
3. **Kernel Kernel**: The existing keyboard/mouse/touchpad modules which expose the sysfs attributes and input devices.

### 7.2 Features

**Real-Time Visualization**:

- **Virtual Keyboard**: Highlights keys on-screen as they are pressed.
- **Mouse Tracker**: Renders cursor movement trails, clicks, and scroll events on an HTML5 canvas.
- **Touchpad Canvas**: Visualizes multi-touch absolute positions and gesture inputs.

**Injection Control**:

- **Text Injection**: Users can type text in the UI ("Hello World") which the backend converts to a sequence of scan codes and injects into the kernel via sysfs.
- **Path Injection**: A canvas editor allows drawing arbitrary paths which are then replayed as mouse movement packets to the driver.
- **Preset Patterns**: Automated patterns like circular motion or button barrages for stress testing.

**Advanced Configuration**:

- **Dynamic Parameters**: Sliders to adjust kernel parameters like `repeat_rate` and `dpi_multiplier` in real-time by writing to sysfs attributes.
- **Statistics**: Live tracking of event rates (EPS), click distribution (pie charts), and cumulative mouse distance.

### 7.3 Technical Challenges

**Event Streaming**: Bridging the blocking `read()` from character devices to the asynchronous WebSocket stream required non-blocking I/O or threading. The Python `asyncio` loop manages this efficiently.

**Latency**: To ensure the "real-time" feel, the dashboard uses binary WebSocket frames where possible and optimized DOM updates (requestAnimationFrame) for the canvas rendering.

## 8. Conclusion

This project provides hands-on experience with kernel module development, interrupt handling, and device driver architecture. The code is educational yet realistic—it follows patterns used in production drivers but remains readable and well-commented. The hardware simulation approach allows testing without physical devices, making it ideal for academic environments.

Key takeaways:

- Device drivers mediate between hardware timing (interrupts) and software abstractions (files, events)
- Proper synchronization is critical in concurrent kernel code
- Linux subsystems (input, device model) provide powerful infrastructure
- Testing strategies must account for kernel-space constraints (no printf, careful memory management)

## 9. Future Enhancements

Potential extensions for advanced students:

- Add scroll wheel support (4-byte PS/2 packets)
- Implement full scan code Set 2 support
- Add ioctl interface for configuration
- Create a virtual USB HID version for comparison
- Add power management (suspend/resume)
- Implement rate limiting and debouncing

## References

1. Linux Device Drivers, 3rd Edition - Corbet, Rubini, Kroah-Hartman
2. Linux Kernel Documentation - Documentation/input/input-programming.txt
3. Linux source code - drivers/input/keyboard/atkbd.c and drivers/input/mouse/psmouse.c
4. PS/2 Interface Specification - <https://www.computer-engineering.org/ps2protocol/>

---

**Course**: Operating Systems  
**Date**: 2025  
**Category**: Device Driver Development, Kernel Programming
