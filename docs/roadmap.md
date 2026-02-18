# KMDD — Weekly Development Roadmap

A week-by-week plan to **improve** the existing Keyboard-Mouse Device Driver project and **add a web-based UI dashboard** for real-time visualization, testing, and analytics.

## Current State (Already Done)

| Component | Status |
|-----------|--------|
| `keyboard_driver.c` — PS/2 keyboard module (scan code → keycode, tasklet, spinlock, sysfs injection) | ✅ Complete |
| `mouse_driver.c` — PS/2 mouse module (3-byte packet parsing, buttons, relative motion) | ✅ Complete |
| `reader.c` — CLI event reader with color output | ✅ Complete |
| `Makefile` — build system for modules + userspace | ✅ Complete |
| `install.sh` — automated load/unload script | ✅ Complete |
| `test_keyboard.sh` / `test_mouse.sh` — automated test scripts | ✅ Complete |
| `docs/lab_report.md` / `docs/presentation.md` — documentation | ✅ Complete |

---

## Week 1 — Driver Improvements & Extended Features ✅

**Goal:** Make the drivers more robust, feature-rich, and closer to production-quality.

### Tasks

- [x] **Extended Scan Code Table** — Add support for F11, F12, arrow keys, Home/End/PgUp/PgDn, Delete, Insert, Numpad keys, and multimedia keys (Volume Up/Down, Mute, Play/Pause)
- [x] **LED Indicator Support** — Add sysfs attributes to simulate Caps Lock, Num Lock, and Scroll Lock LED states
- [x] **Keyboard Repeat Rate** — Add module parameters to configure key repeat delay and rate
- [x] **Mouse Scroll Wheel** — Extend mouse driver to support 4-byte IntelliMouse packets with scroll wheel (REL_WHEEL)
- [x] **Mouse Sensitivity / DPI** — Add a configurable DPI multiplier via sysfs
- [x] **Statistics via `/proc`** — Create `/proc/vkbd_stats` and `/proc/vmouse_stats` to expose key press counts, packets processed, buffer overflows, uptime, etc.
- [x] **Improved Error Handling** — Add rate-limited warnings, overflow counters, and recovery logic

### Deliverables

- Updated `keyboard_driver.c` and `mouse_driver.c`
- New `/proc` entries
- Updated test scripts

---

## Week 2 — Advanced Driver Features & Logger Daemon ✅

**Goal:** Add kernel-level advanced features and a background logging daemon.

### Tasks

- [x] **Combo/Macro Key Detection** — Detect multi-key combos (e.g., Ctrl+C, Ctrl+Z, Alt+Tab) and log them as named events
- [x] **Extended Mouse Buttons** — Support for Side/Forward/Back buttons (BTN_SIDE, BTN_EXTRA)
- [x] **Touchpad Simulation** — Add a new `touchpad_driver.c` module simulating absolute positioning (EV_ABS) with single-tap and two-finger scroll gestures
- [x] **Logger Daemon (`event_logger.c`)** — A background user-space daemon that reads `/dev/input/eventX` and writes structured logs (JSON format) to `logs/events.json` with timestamps, event types, and decoded values
- [x] **Log Rotation & Filtering** — Logger supports max file size, rotation, and CLI flags to filter by device or event type
- [x] **Unit Tests** — Add a C-based test framework or structured script tests to validate scan code tables, packet parsing logic, and buffer edge cases

### Deliverables

- New `touchpad_driver.c`
- New `userspace/event_logger.c`
- `logs/` directory with JSON output
- Expanded `tests/` suite

---

## Week 3 — Web UI Dashboard (Frontend Foundation) ✅

**Goal:** Build a beautiful real-time web dashboard to visualize and control the drivers.

### Tasks

- [x] **Web Server Backend** — Python asyncio HTTP + WebSocket server (`backend/server.py`) that reads from `/dev/input/eventX` and streams events to the browser
- [x] **Dashboard Layout** — Dark-themed glassmorphism HTML/CSS/JS dashboard with:
  - Header with project name, system status indicators, and module load status
  - Navigation tabs: **Live Events**, **Keyboard**, **Mouse**, **Statistics**, **Settings**
- [x] **Live Events Panel** — Real-time scrolling event feed showing keyboard presses, mouse movement, and button clicks with color-coded badges and timestamps
- [x] **Virtual Keyboard Visualization** — Interactive on-screen keyboard layout that:
  - Highlights keys in real-time as events arrive
  - Shows key press/release animations
  - Supports clicking keys to inject scan codes (via backend → sysfs)

### Deliverables

- `ui/` directory with `index.html`, `styles.css`, `app.js`
- `backend/server.py`
- WebSocket-based live streaming

---

## Week 4 — Web UI Dashboard (Advanced Features) ✅

**Goal:** Complete the dashboard with mouse visualization, stats, and injection controls.

### Tasks

- [x] **Mouse Visualization Panel** — Canvas-based mouse tracker showing:
  - Real-time cursor trail with fade effect
  - Button state indicators (L/M/R) with animation
  - Movement vector arrows
  - Scroll wheel indicator
- [x] **Statistics Dashboard** — Charts and counters:
  - Key frequency top-keys chips (most-pressed keys)
  - Events-per-second bar (live)
  - Total events count, uptime, click count
  - [x] Button click distribution pie chart
  - [x] Mouse movement distance tracking
- [x] **Injection Control Panel** — UI to send test inputs:
  - Text-to-scancode converter (type a sentence → inject all scancodes)
  - Mouse waypoint path editor (draw a path → inject sequence of packets)
  - Preset test patterns (type "HELLO", circle motion, button barrage)
- [x] **Settings Panel** — All settings implemented:
  - WebSocket URL, auto-reconnect, max events, show SYN events
  - [x] Keyboard repeat rate/delay
  - [x] Mouse DPI / sensitivity
  - [x] LED toggle controls
  - [x] Log level control

### Deliverables

- Complete `ui/` with all panels
- Full WebSocket bidirectional communication
- Polished animations and interactions

---

## Week 5 — Testing, Documentation, & Polish ✅

**Goal:** Comprehensive testing, documentation update, and final polish.

### Tasks

- [x] **End-to-End Test Automation** — Script that loads modules, starts backend, runs injection tests, and validates events arrive in the UI (headless browser test with Puppeteer/Playwright or manual checklist)
- [x] **Touchpad Panel** — Add touchpad visualization to the UI (if touchpad driver done in Week 2)
- [x] **Update Documentation** — Complete rewrite of `README.md` showcasing the UI with screenshots, updated architecture diagrams, and setup instructions
- [x] **Lab Report Update** — Update `docs/lab_report.md` with new sections on UI, logger, touchpad, and advanced features
- [x] **Presentation Update** — Update `docs/presentation.md` with demo slides and screenshots
- [x] **Performance Profiling** — Test with rapid event injection (1000+ events/sec) to verify no drops
- [x] **Code Cleanup** — Consistent formatting, complete comments, remove dead code

### Deliverables

- Full test suite with results
- Updated docs and presentation
- Performance benchmark results
- Production-ready codebase

---

## Summary Timeline

| Week | Focus | Key Outcome | Status |
|------|-------|-------------|--------|
| **1** | Driver Improvements | Extended keys, scroll wheel, stats, LED support | ✅ Done |
| **2** | Advanced Features & Logger | Touchpad, combos, JSON logger, unit tests | ✅ Done |
| **3** | Web UI Foundation | Dashboard, live events, virtual keyboard viz | ✅ Done |
| **4** | Web UI Advanced | Mouse viz, stats charts, injection panel, settings | ✅ Done |
| **5** | Testing & Polish | E2E tests, docs, performance, final polish | ✅ Done |

---

## Verification Plan
>
> [!NOTE]
> See [Verification Report](verification_report.md) for detailed results.

### Automated Tests

- **Module load/unload**: `sudo insmod` / `sudo rmmod` + check `lsmod` and `dmesg`
- **Scan code injection**: Run updated `test_keyboard.sh` / `test_mouse.sh` and verify expanded key coverage
- **Logger output**: Run `event_logger`, inject events, validate `logs/events.json` contains correct structured entries
- **UI WebSocket**: Start backend server, open dashboard in browser, inject events, verify they appear in live feed within < 500ms

### Manual Verification

- **Visual keyboard**: Load dashboard → inject key events → confirm correct keys highlight on the virtual keyboard
- **Mouse trail**: Inject mouse movement packets → verify canvas shows smooth cursor trail
- **Statistics**: Run a series of events → confirm heatmap, charts, and counters update accurately
- **Settings**: Change DPI via UI → inject mouse movement → verify scaled movement values in event feed

> [!NOTE]
> Each week builds on the previous one. We will review progress at the end of each week and adjust the plan as needed.
