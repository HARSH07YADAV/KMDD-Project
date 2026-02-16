#!/usr/bin/env python3
"""
KMDD Web Dashboard Server
HTTP static file server + WebSocket event streaming.

Usage:
    python3 server.py                          # Live mode (reads /dev/input/eventX)
    python3 server.py --simulate               # Simulation mode (fake events)
    python3 server.py --port 9090 --ws-port 9091  # Custom ports
    python3 server.py --device /dev/input/event5   # Specific device

License: MIT
"""

import asyncio
import json
import os
import sys
import struct
import time
import random
import argparse
import glob
import signal
from http.server import HTTPServer, SimpleHTTPRequestHandler
from threading import Thread
from pathlib import Path

try:
    import websockets
except ImportError:
    print("ERROR: 'websockets' package not found.")
    print("Install with: pip3 install websockets")
    sys.exit(1)

# ── Constants ──────────────────────────────────────────────────
INPUT_EVENT_FORMAT = 'llHHI'  # struct input_event: time_sec, time_usec, type, code, value
INPUT_EVENT_SIZE = struct.calcsize(INPUT_EVENT_FORMAT)

# Event types
EV_SYN = 0x00
EV_KEY = 0x01
EV_REL = 0x02
EV_ABS = 0x03
EV_MSC = 0x04
EV_LED = 0x11
EV_REP = 0x14

# Relative axes
REL_X = 0x00
REL_Y = 0x01
REL_WHEEL = 0x08
REL_HWHEEL = 0x06

# ── Key Code Maps ─────────────────────────────────────────────
KEYCODE_NAMES = {
    1: "ESC", 2: "1", 3: "2", 4: "3", 5: "4", 6: "5", 7: "6", 8: "7",
    9: "8", 10: "9", 11: "0", 12: "MINUS", 13: "EQUAL", 14: "BACKSPACE",
    15: "TAB", 16: "Q", 17: "W", 18: "E", 19: "R", 20: "T", 21: "Y",
    22: "U", 23: "I", 24: "O", 25: "P", 26: "[", 27: "]", 28: "ENTER",
    29: "L_CTRL", 30: "A", 31: "S", 32: "D", 33: "F", 34: "G", 35: "H",
    36: "J", 37: "K", 38: "L", 39: ";", 40: "'", 41: "GRAVE", 42: "L_SHIFT",
    43: "\\", 44: "Z", 45: "X", 46: "C", 47: "V", 48: "B", 49: "N",
    50: "M", 51: ",", 52: ".", 53: "/", 54: "R_SHIFT", 55: "KP_*",
    56: "L_ALT", 57: "SPACE", 58: "CAPS_LOCK", 59: "F1", 60: "F2",
    61: "F3", 62: "F4", 63: "F5", 64: "F6", 65: "F7", 66: "F8",
    67: "F9", 68: "F10", 69: "NUM_LOCK", 70: "SCROLL_LK",
    71: "KP_7", 72: "KP_8", 73: "KP_9", 74: "KP_-",
    75: "KP_4", 76: "KP_5", 77: "KP_6", 78: "KP_+",
    79: "KP_1", 80: "KP_2", 81: "KP_3", 82: "KP_0", 83: "KP_.",
    87: "F11", 88: "F12",
    96: "KP_ENTER", 97: "R_CTRL", 98: "KP_/",
    100: "R_ALT", 102: "HOME", 103: "UP", 104: "PAGE_UP",
    105: "LEFT", 106: "RIGHT", 107: "END", 108: "DOWN", 109: "PAGE_DN",
    110: "INSERT", 111: "DELETE",
    113: "MUTE", 114: "VOL-", 115: "VOL+",
    125: "L_META", 126: "R_META", 127: "MENU",
    163: "NEXT", 164: "PLAY/PAUSE", 165: "PREV", 166: "STOP",
    # Mouse buttons
    272: "BTN_LEFT", 273: "BTN_RIGHT", 274: "BTN_MIDDLE",
    275: "BTN_SIDE", 276: "BTN_EXTRA", 330: "BTN_TOUCH",
}

EVENT_TYPE_NAMES = {
    EV_SYN: "SYN", EV_KEY: "KEY", EV_REL: "REL", EV_ABS: "ABS",
    EV_MSC: "MSC", EV_LED: "LED", EV_REP: "REP",
}

REL_AXIS_NAMES = {
    REL_X: "X", REL_Y: "Y", REL_WHEEL: "WHEEL", REL_HWHEEL: "HWHEEL",
}

# ── Global State ───────────────────────────────────────────────
connected_clients = set()
event_counter = 0
server_start_time = time.time()


def find_virtual_devices():
    """Find virtual input devices created by our drivers."""
    devices = []
    try:
        with open('/proc/bus/input/devices', 'r') as f:
            content = f.read()
        blocks = content.split('\n\n')
        for block in blocks:
            if 'Virtual' in block:
                for line in block.split('\n'):
                    if line.startswith('H: Handlers='):
                        handlers = line.split('=')[1]
                        for h in handlers.split():
                            if h.startswith('event'):
                                devices.append(f'/dev/input/{h}')
    except Exception:
        pass
    return devices


def find_sysfs_inject_path(kind='scancode'):
    """Find sysfs injection path for keyboard or mouse."""
    patterns = {
        'scancode': '/sys/devices/virtual/input/input*/inject_scancode',
        'packet': '/sys/devices/virtual/input/input*/inject_packet',
        'tap': '/sys/devices/virtual/input/input*/inject_tap',
    }
    matches = glob.glob(patterns.get(kind, ''))
    return matches[0] if matches else None


def parse_input_event(data):
    """Parse a raw Linux input_event struct into a dict."""
    global event_counter
    sec, usec, ev_type, code, value = struct.unpack(INPUT_EVENT_FORMAT, data)

    if ev_type == EV_SYN:
        return None  # Skip sync events

    event_counter += 1
    event = {
        'id': event_counter,
        'time': f"{time.strftime('%H:%M:%S')}.{int(time.time() * 1000) % 1000:03d}",
        'type': EVENT_TYPE_NAMES.get(ev_type, f'UNK_{ev_type}'),
        'type_id': ev_type,
        'code': code,
        'value': value,
    }

    if ev_type == EV_KEY:
        event['key'] = KEYCODE_NAMES.get(code, f'KEY_{code}')
        event['action'] = 'repeat' if value == 2 else ('press' if value == 1 else 'release')
    elif ev_type == EV_REL:
        event['axis'] = REL_AXIS_NAMES.get(code, f'REL_{code}')
    elif ev_type == EV_ABS:
        abs_names = {0: 'ABS_X', 1: 'ABS_Y', 24: 'ABS_PRESSURE',
                     47: 'ABS_MT_SLOT', 53: 'ABS_MT_X', 54: 'ABS_MT_Y',
                     58: 'ABS_MT_PRESSURE', 57: 'ABS_MT_TRACKING_ID'}
        event['axis'] = abs_names.get(code, f'ABS_{code}')

    return event


async def broadcast(message):
    """Send a message to all connected WebSocket clients."""
    if connected_clients:
        data = json.dumps(message)
        await asyncio.gather(
            *[client.send(data) for client in connected_clients],
            return_exceptions=True
        )


# ── Device Reader ──────────────────────────────────────────────
async def read_device(device_path):
    """Read events from a real input device and broadcast them."""
    print(f"  📡 Reading from: {device_path}")
    try:
        fd = os.open(device_path, os.O_RDONLY | os.O_NONBLOCK)
    except PermissionError:
        print(f"  ⚠  Permission denied: {device_path} (try running with sudo)")
        return
    except FileNotFoundError:
        print(f"  ⚠  Device not found: {device_path}")
        return

    loop = asyncio.get_event_loop()
    reader = asyncio.StreamReader()

    while True:
        try:
            data = await loop.run_in_executor(None, lambda: _read_event(fd))
            if data:
                event = parse_input_event(data)
                if event:
                    await broadcast(event)
        except Exception:
            await asyncio.sleep(0.01)


def _read_event(fd):
    """Blocking read of one input_event from fd."""
    try:
        return os.read(fd, INPUT_EVENT_SIZE)
    except BlockingIOError:
        time.sleep(0.01)
        return None
    except OSError:
        time.sleep(0.1)
        return None


# ── Simulation Mode ────────────────────────────────────────────
SIM_KEYS = [
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  # Q-P
    30, 31, 32, 33, 34, 35, 36, 37, 38,       # A-L
    44, 45, 46, 47, 48, 49, 50,                # Z-M
    57,  # SPACE
    28,  # ENTER
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11,           # 1-0
]


async def simulate_events():
    """Generate simulated keyboard and mouse events for demo mode."""
    global event_counter
    print("  🎮 Simulation mode active — generating fake events")

    while True:
        await asyncio.sleep(random.uniform(0.3, 1.5))

        choice = random.random()
        if choice < 0.5:
            # Simulate key press + release
            key = random.choice(SIM_KEYS)
            event_counter += 1
            await broadcast({
                'id': event_counter,
                'time': f"{time.strftime('%H:%M:%S')}.{int(time.time() * 1000) % 1000:03d}",
                'type': 'KEY', 'type_id': EV_KEY,
                'code': key, 'value': 1,
                'key': KEYCODE_NAMES.get(key, f'KEY_{key}'),
                'action': 'press',
            })
            await asyncio.sleep(random.uniform(0.05, 0.15))
            event_counter += 1
            await broadcast({
                'id': event_counter,
                'time': f"{time.strftime('%H:%M:%S')}.{int(time.time() * 1000) % 1000:03d}",
                'type': 'KEY', 'type_id': EV_KEY,
                'code': key, 'value': 0,
                'key': KEYCODE_NAMES.get(key, f'KEY_{key}'),
                'action': 'release',
            })
        elif choice < 0.8:
            # Simulate mouse movement
            dx = random.randint(-20, 20)
            dy = random.randint(-20, 20)
            for axis_code, axis_name, val in [(REL_X, 'X', dx), (REL_Y, 'Y', dy)]:
                if val != 0:
                    event_counter += 1
                    await broadcast({
                        'id': event_counter,
                        'time': f"{time.strftime('%H:%M:%S')}.{int(time.time() * 1000) % 1000:03d}",
                        'type': 'REL', 'type_id': EV_REL,
                        'code': axis_code, 'value': val,
                        'axis': axis_name,
                    })
        else:
            # Simulate mouse button click
            btn = random.choice([272, 273, 274])
            event_counter += 1
            await broadcast({
                'id': event_counter,
                'time': f"{time.strftime('%H:%M:%S')}.{int(time.time() * 1000) % 1000:03d}",
                'type': 'KEY', 'type_id': EV_KEY,
                'code': btn, 'value': 1,
                'key': KEYCODE_NAMES.get(btn, f'BTN_{btn}'),
                'action': 'press',
            })
            await asyncio.sleep(0.1)
            event_counter += 1
            await broadcast({
                'id': event_counter,
                'time': f"{time.strftime('%H:%M:%S')}.{int(time.time() * 1000) % 1000:03d}",
                'type': 'KEY', 'type_id': EV_KEY,
                'code': btn, 'value': 0,
                'key': KEYCODE_NAMES.get(btn, f'BTN_{btn}'),
                'action': 'release',
            })


# ── Injection ──────────────────────────────────────────────────

# Text-to-scancode mapping (lowercase + common chars)
TEXT_TO_SCANCODE = {
    'a': 0x1E, 'b': 0x30, 'c': 0x2E, 'd': 0x20, 'e': 0x12,
    'f': 0x21, 'g': 0x22, 'h': 0x23, 'i': 0x17, 'j': 0x24,
    'k': 0x25, 'l': 0x26, 'm': 0x32, 'n': 0x31, 'o': 0x18,
    'p': 0x19, 'q': 0x10, 'r': 0x13, 's': 0x1F, 't': 0x14,
    'u': 0x16, 'v': 0x2F, 'w': 0x11, 'x': 0x2D, 'y': 0x15,
    'z': 0x2C, '1': 0x02, '2': 0x03, '3': 0x04, '4': 0x05,
    '5': 0x06, '6': 0x07, '7': 0x08, '8': 0x09, '9': 0x0A,
    '0': 0x0B, ' ': 0x39, '\n': 0x1C, '\t': 0x0F, '-': 0x0C,
    '=': 0x0D, '[': 0x1A, ']': 0x1B, '\\': 0x2B, ';': 0x27,
    "'": 0x28, '`': 0x29, ',': 0x33, '.': 0x34, '/': 0x35,
}

# Characters that need Shift held
SHIFT_CHARS = {
    'A': 'a', 'B': 'b', 'C': 'c', 'D': 'd', 'E': 'e', 'F': 'f',
    'G': 'g', 'H': 'h', 'I': 'i', 'J': 'j', 'K': 'k', 'L': 'l',
    'M': 'm', 'N': 'n', 'O': 'o', 'P': 'p', 'Q': 'q', 'R': 'r',
    'S': 's', 'T': 't', 'U': 'u', 'V': 'v', 'W': 'w', 'X': 'x',
    'Y': 'y', 'Z': 'z', '!': '1', '@': '2', '#': '3', '$': '4',
    '%': '5', '^': '6', '&': '7', '*': '8', '(': '9', ')': '0',
    '_': '-', '+': '=', '{': '[', '}': ']', '|': '\\', ':': ';',
    '"': "'", '~': '`', '<': ',', '>': '.', '?': '/',
}

# Log level state
current_log_level = 'INFO'


def inject_scancode(scancode_hex):
    """Inject a scan code via sysfs."""
    path = find_sysfs_inject_path('scancode')
    if not path:
        return {'status': 'error', 'message': 'Keyboard sysfs path not found'}
    try:
        with open(path, 'w') as f:
            f.write(scancode_hex)
        return {'status': 'ok', 'message': f'Injected {scancode_hex}'}
    except Exception as e:
        return {'status': 'error', 'message': str(e)}


def inject_mouse_packet(buttons, dx, dy):
    """Inject a mouse packet via sysfs."""
    path = find_sysfs_inject_path('packet')
    if not path:
        return {'status': 'error', 'message': 'Mouse sysfs path not found'}
    try:
        packet = f"0x{buttons:02X} 0x{dx & 0xFF:02X} 0x{dy & 0xFF:02X}"
        with open(path, 'w') as f:
            f.write(packet)
        return {'status': 'ok', 'message': f'Injected packet: {packet}'}
    except Exception as e:
        return {'status': 'error', 'message': str(e)}


def inject_text_string(text):
    """Convert text to scancodes and inject each character."""
    injected = 0
    for ch in text:
        needs_shift = ch in SHIFT_CHARS
        base_char = SHIFT_CHARS.get(ch, ch)
        scancode = TEXT_TO_SCANCODE.get(base_char)
        if scancode is None:
            continue
        if needs_shift:
            inject_scancode('0x2A')  # L_SHIFT press
        inject_scancode(f'0x{scancode:02X}')  # key press
        inject_scancode(f'0x{scancode | 0x80:02X}')  # key release
        if needs_shift:
            inject_scancode('0xAA')  # L_SHIFT release
        injected += 1
    return {'status': 'ok', 'message': f'Injected {injected} characters'}


def inject_path_waypoints(waypoints):
    """Inject a series of mouse movement waypoints as relative packets."""
    if not waypoints or len(waypoints) < 2:
        return {'status': 'error', 'message': 'Need at least 2 waypoints'}
    injected = 0
    for i in range(1, len(waypoints)):
        dx = waypoints[i].get('x', 0) - waypoints[i-1].get('x', 0)
        dy = waypoints[i].get('y', 0) - waypoints[i-1].get('y', 0)
        # Clamp to signed byte range
        dx = max(-127, min(127, dx))
        dy = max(-127, min(127, dy))
        inject_mouse_packet(0, dx, dy)
        injected += 1
    return {'status': 'ok', 'message': f'Injected {injected} waypoint segments'}


import math

def inject_preset_pattern(preset_name):
    """Run a preset injection pattern."""
    if preset_name == 'hello':
        return inject_text_string('HELLO')
    elif preset_name == 'circle':
        steps = 36
        radius = 30
        for i in range(steps + 1):
            angle = 2 * math.pi * i / steps
            dx = int(radius * math.cos(angle) - radius * math.cos(2 * math.pi * (i-1) / steps)) if i > 0 else 0
            dy = int(radius * math.sin(angle) - radius * math.sin(2 * math.pi * (i-1) / steps)) if i > 0 else 0
            inject_mouse_packet(0, dx & 0xFF, dy & 0xFF)
        return {'status': 'ok', 'message': f'Injected circle pattern ({steps} steps)'}
    elif preset_name == 'button_barrage':
        for btn_code in [272, 273, 274] * 3:  # L, R, M x3
            scan_press = f'0x{btn_code:02X}'
            # For button barrage in sim mode we just send events
            inject_scancode(scan_press)
        return {'status': 'ok', 'message': 'Injected button barrage (9 clicks)'}
    return {'status': 'error', 'message': f'Unknown preset: {preset_name}'}


def write_sysfs_attr(attr_name, value):
    """Write a value to a sysfs attribute in the keyboard/mouse driver directory."""
    for pattern in [
        f'/sys/devices/virtual/input/input*/{attr_name}',
    ]:
        matches = glob.glob(pattern)
        if matches:
            try:
                with open(matches[0], 'w') as f:
                    f.write(str(value))
                return {'status': 'ok', 'message': f'Set {attr_name} = {value}'}
            except Exception as e:
                return {'status': 'error', 'message': str(e)}
    return {'status': 'error', 'message': f'Sysfs attribute {attr_name} not found'}


# ── WebSocket Handler ─────────────────────────────────────────
async def ws_handler(websocket):
    """Handle a WebSocket client connection."""
    global current_log_level
    connected_clients.add(websocket)
    client_addr = websocket.remote_address
    print(f"  🔗 Client connected: {client_addr}")

    # Send initial status
    devices = find_virtual_devices()
    await websocket.send(json.dumps({
        'type': 'status',
        'devices': devices,
        'uptime': int(time.time() - server_start_time),
        'event_count': event_counter,
        'simulate': args.simulate,
    }))

    try:
        async for message in websocket:
            try:
                cmd = json.loads(message)
                action = cmd.get('action', '')

                if action == 'inject_key':
                    scancode = cmd.get('scancode', '0x00')
                    result = inject_scancode(scancode)
                    # Also inject release
                    release_code = int(scancode, 16) | 0x80
                    inject_scancode(f'0x{release_code:02X}')
                    await websocket.send(json.dumps({
                        'type': 'inject_result', **result
                    }))

                elif action == 'inject_mouse':
                    buttons = cmd.get('buttons', 0)
                    dx = cmd.get('dx', 0)
                    dy = cmd.get('dy', 0)
                    result = inject_mouse_packet(buttons, dx, dy)
                    await websocket.send(json.dumps({
                        'type': 'inject_result', **result
                    }))

                elif action == 'inject_text':
                    text = cmd.get('text', '')
                    result = inject_text_string(text)
                    await websocket.send(json.dumps({
                        'type': 'inject_result', **result
                    }))

                elif action == 'inject_preset':
                    preset = cmd.get('preset', '')
                    result = inject_preset_pattern(preset)
                    await websocket.send(json.dumps({
                        'type': 'inject_result', **result
                    }))

                elif action == 'inject_path':
                    waypoints = cmd.get('waypoints', [])
                    result = inject_path_waypoints(waypoints)
                    await websocket.send(json.dumps({
                        'type': 'inject_result', **result
                    }))

                elif action == 'set_repeat':
                    delay = cmd.get('delay')
                    rate = cmd.get('rate')
                    results = []
                    if delay is not None:
                        results.append(write_sysfs_attr('repeat_delay_ms', int(delay)))
                    if rate is not None:
                        results.append(write_sysfs_attr('repeat_rate_ms', int(rate)))
                    msg = '; '.join(r['message'] for r in results)
                    await websocket.send(json.dumps({
                        'type': 'inject_result', 'status': 'ok', 'message': msg or 'No values set'
                    }))

                elif action == 'set_dpi':
                    dpi = cmd.get('dpi', 1)
                    result = write_sysfs_attr('dpi_multiplier', int(dpi))
                    await websocket.send(json.dumps({
                        'type': 'inject_result', **result
                    }))

                elif action == 'set_led':
                    led = cmd.get('led', '')  # caps, num, scroll
                    state = cmd.get('state', 0)
                    attr_map = {'caps': 'led_caps', 'num': 'led_num', 'scroll': 'led_scroll'}
                    attr = attr_map.get(led)
                    if attr:
                        result = write_sysfs_attr(attr, int(state))
                    else:
                        result = {'status': 'error', 'message': f'Unknown LED: {led}'}
                    await websocket.send(json.dumps({
                        'type': 'inject_result', **result
                    }))

                elif action == 'set_log_level':
                    level = cmd.get('level', 'INFO')
                    current_log_level = level
                    await websocket.send(json.dumps({
                        'type': 'inject_result', 'status': 'ok',
                        'message': f'Log level set to {level}'
                    }))

                elif action == 'get_status':
                    devices = find_virtual_devices()
                    await websocket.send(json.dumps({
                        'type': 'status',
                        'devices': devices,
                        'uptime': int(time.time() - server_start_time),
                        'event_count': event_counter,
                        'simulate': args.simulate,
                    }))

            except json.JSONDecodeError:
                await websocket.send(json.dumps({
                    'type': 'error', 'message': 'Invalid JSON'
                }))

    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        connected_clients.discard(websocket)
        print(f"  ❌ Client disconnected: {client_addr}")


# ── HTTP Server ────────────────────────────────────────────────
class DashboardHTTPHandler(SimpleHTTPRequestHandler):
    """Serve the UI files from the ui/ directory."""

    def __init__(self, *a, **kw):
        ui_dir = str(Path(__file__).parent.parent / 'ui')
        super().__init__(*a, directory=ui_dir, **kw)

    def log_message(self, format, *a):
        # Suppress default logging noise
        pass

    def end_headers(self):
        # Add CORS headers for development
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()


def run_http_server(port):
    """Run the HTTP server in a background thread."""
    server = HTTPServer(('0.0.0.0', port), DashboardHTTPHandler)
    server.serve_forever()


# ── Main ───────────────────────────────────────────────────────
def parse_args():
    parser = argparse.ArgumentParser(
        description='KMDD Web Dashboard Server',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 server.py --simulate           # Demo without kernel modules
  python3 server.py --device /dev/input/event5
  sudo python3 server.py                 # Auto-detect virtual devices
        """
    )
    parser.add_argument('--port', type=int, default=8080,
                        help='HTTP server port (default: 8080)')
    parser.add_argument('--ws-port', type=int, default=8765,
                        help='WebSocket server port (default: 8765)')
    parser.add_argument('--device', type=str, default=None,
                        help='Input device path (auto-detect if omitted)')
    parser.add_argument('--simulate', action='store_true',
                        help='Simulation mode: generate fake events')
    return parser.parse_args()


async def main():
    global args
    args = parse_args()

    print()
    print("╔═══════════════════════════════════════════════╗")
    print("║  KMDD Web Dashboard Server                   ║")
    print("╠═══════════════════════════════════════════════╣")
    print(f"║  HTTP:      http://localhost:{args.port:<17}║")
    print(f"║  WebSocket: ws://localhost:{args.ws_port:<18}║")
    print(f"║  Mode:      {'Simulation' if args.simulate else 'Live':<28}║")
    print("╠═══════════════════════════════════════════════╣")
    print("║  Press Ctrl+C to stop                        ║")
    print("╚═══════════════════════════════════════════════╝")
    print()

    # Start HTTP server in background thread
    http_thread = Thread(target=run_http_server, args=(args.port,), daemon=True)
    http_thread.start()
    print(f"  🌐 HTTP server running on port {args.port}")

    # Start WebSocket server
    ws_server = await websockets.serve(ws_handler, '0.0.0.0', args.ws_port)
    print(f"  🔌 WebSocket server running on port {args.ws_port}")

    if args.simulate:
        # Simulation mode
        await simulate_events()
    else:
        # Live mode — find and read devices
        device_path = args.device
        if not device_path:
            devices = find_virtual_devices()
            if devices:
                device_path = devices[0]
                print(f"  📡 Auto-detected device: {device_path}")
            else:
                print("  ⚠  No virtual devices found. Use --simulate or --device")
                print("     Waiting for device injection commands...")
                # Just keep the server running
                await asyncio.Future()  # Block forever

        await read_device(device_path)


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n  👋 Server stopped.")
