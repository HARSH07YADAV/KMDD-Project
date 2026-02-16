#!/bin/bash
# ════════════════════════════════════════════════════════════
#  KMDD — End-to-End Test Script
#  Tests the full stack: backend server → WebSocket → UI
# ════════════════════════════════════════════════════════════

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BACKEND="${PROJECT_DIR}/backend/server.py"
UI_DIR="${PROJECT_DIR}/ui"
SERVER_PID=""
PASS=0
FAIL=0
TOTAL=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        echo -e "\n${CYAN}[CLEANUP]${NC} Stopping server (PID: $SERVER_PID)…"
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

log_test() {
    TOTAL=$((TOTAL + 1))
    echo -e "${CYAN}[TEST ${TOTAL}]${NC} $1"
}

pass() {
    PASS=$((PASS + 1))
    echo -e "  ${GREEN}✓ PASS${NC}: $1"
}

fail() {
    FAIL=$((FAIL + 1))
    echo -e "  ${RED}✗ FAIL${NC}: $1"
}

echo "═══════════════════════════════════════════════════════"
echo " KMDD End-to-End Test Suite"
echo "═══════════════════════════════════════════════════════"
echo ""

# ─── Prerequisites ─────────────────────────────────────────
log_test "Checking prerequisites"

if ! command -v python3 &>/dev/null; then
    fail "python3 not found"
    exit 1
fi
pass "python3 available"

if ! python3 -c "import websockets" 2>/dev/null; then
    echo -e "  ${YELLOW}⚠ websockets not installed, attempting install…${NC}"
    pip3 install websockets --quiet 2>/dev/null || true
    if ! python3 -c "import websockets" 2>/dev/null; then
        fail "websockets module not available"
        exit 1
    fi
fi
pass "websockets module available"

if [ ! -f "$BACKEND" ]; then
    fail "Backend server not found at $BACKEND"
    exit 1
fi
pass "Backend server script exists"

if [ ! -f "${UI_DIR}/index.html" ]; then
    fail "UI files not found"
    exit 1
fi
pass "UI files exist"

# ─── Start Backend Server ──────────────────────────────────
log_test "Starting backend server in simulation mode"

python3 "$BACKEND" --simulate --port 8765 &
SERVER_PID=$!
sleep 2

if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "Server started (PID: $SERVER_PID)"
else
    fail "Server failed to start"
    exit 1
fi

# ─── HTTP Test ─────────────────────────────────────────────
log_test "HTTP server is serving UI files"

HTTP_STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/ 2>/dev/null || echo "000")
if [ "$HTTP_STATUS" = "200" ]; then
    pass "HTTP GET / returned 200"
else
    fail "HTTP GET / returned status $HTTP_STATUS (expected 200)"
fi

# Check that index.html content is served
HTTP_BODY=$(curl -s http://localhost:8080/ 2>/dev/null || echo "")
if echo "$HTTP_BODY" | grep -q "KMDD Dashboard"; then
    pass "index.html served with correct title"
else
    fail "index.html content not found in HTTP response"
fi

# ─── WebSocket Connection Test ─────────────────────────────
log_test "WebSocket connection and status message"

WS_RESULT=$(python3 -c "
import asyncio, json, websockets

async def test():
    async with websockets.connect('ws://localhost:8765') as ws:
        msg = await asyncio.wait_for(ws.recv(), timeout=5)
        data = json.loads(msg)
        if data.get('type') == 'status' and data.get('simulate') == True:
            print('OK:status_received')
        else:
            print(f'FAIL:unexpected_data:{json.dumps(data)[:100]}')

asyncio.run(test())
" 2>/dev/null || echo "FAIL:connection_error")

if echo "$WS_RESULT" | grep -q "OK:status_received"; then
    pass "WebSocket connected, received status with simulate=true"
else
    fail "WebSocket status check: $WS_RESULT"
fi

# ─── Simulated Event Streaming Test ────────────────────────
log_test "Simulated events are streaming"

WS_EVENTS=$(python3 -c "
import asyncio, json, websockets

async def test():
    async with websockets.connect('ws://localhost:8765') as ws:
        # Skip initial status
        await asyncio.wait_for(ws.recv(), timeout=5)
        # Collect a few events
        count = 0
        for _ in range(3):
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=3)
                data = json.loads(msg)
                if data.get('type') in ['KEY', 'REL', 'ABS']:
                    count += 1
            except asyncio.TimeoutError:
                break
        print(f'EVENTS:{count}')

asyncio.run(test())
" 2>/dev/null || echo "EVENTS:0")

EVENT_COUNT=$(echo "$WS_EVENTS" | grep -oP 'EVENTS:\K\d+')
if [ "${EVENT_COUNT:-0}" -ge 1 ]; then
    pass "Received $EVENT_COUNT simulated events"
else
    fail "No simulated events received"
fi

# ─── Injection Command Test ────────────────────────────────
log_test "Injection command: inject_text"

INJECT_RESULT=$(python3 -c "
import asyncio, json, websockets

async def test():
    async with websockets.connect('ws://localhost:8765') as ws:
        # Skip status
        await asyncio.wait_for(ws.recv(), timeout=5)
        # Send inject text command
        cmd = json.dumps({'action': 'inject_text', 'text': 'Hi'})
        await ws.send(cmd)
        # Wait for result
        for _ in range(10):
            msg = await asyncio.wait_for(ws.recv(), timeout=3)
            data = json.loads(msg)
            if data.get('type') == 'inject_result':
                print(f'OK:{data.get(\"message\",\"\")}')
                return
        print('FAIL:no_inject_result')

asyncio.run(test())
" 2>/dev/null || echo "FAIL:error")

if echo "$INJECT_RESULT" | grep -q "OK:"; then
    pass "inject_text accepted: ${INJECT_RESULT#OK:}"
else
    fail "inject_text failed: $INJECT_RESULT"
fi

# ─── Preset Pattern Test ──────────────────────────────────
log_test "Injection command: preset 'hello'"

PRESET_RESULT=$(python3 -c "
import asyncio, json, websockets

async def test():
    async with websockets.connect('ws://localhost:8765') as ws:
        await asyncio.wait_for(ws.recv(), timeout=5)
        cmd = json.dumps({'action': 'inject_preset', 'preset': 'hello'})
        await ws.send(cmd)
        for _ in range(10):
            msg = await asyncio.wait_for(ws.recv(), timeout=3)
            data = json.loads(msg)
            if data.get('type') == 'inject_result':
                print(f'OK:{data.get(\"message\",\"\")}')
                return
        print('FAIL:no_result')

asyncio.run(test())
" 2>/dev/null || echo "FAIL:error")

if echo "$PRESET_RESULT" | grep -q "OK:"; then
    pass "preset 'hello' accepted: ${PRESET_RESULT#OK:}"
else
    fail "preset 'hello' failed: $PRESET_RESULT"
fi

# ─── Settings Command Test ─────────────────────────────────
log_test "Settings command: set_log_level"

SETTINGS_RESULT=$(python3 -c "
import asyncio, json, websockets

async def test():
    async with websockets.connect('ws://localhost:8765') as ws:
        await asyncio.wait_for(ws.recv(), timeout=5)
        cmd = json.dumps({'action': 'set_log_level', 'level': 'DEBUG'})
        await ws.send(cmd)
        for _ in range(10):
            msg = await asyncio.wait_for(ws.recv(), timeout=3)
            data = json.loads(msg)
            if data.get('type') == 'inject_result':
                print(f'OK:{data.get(\"message\",\"\")}')
                return
        print('FAIL:no_result')

asyncio.run(test())
" 2>/dev/null || echo "FAIL:error")

if echo "$SETTINGS_RESULT" | grep -q "OK:"; then
    pass "set_log_level accepted: ${SETTINGS_RESULT#OK:}"
else
    fail "set_log_level failed: $SETTINGS_RESULT"
fi

# ─── UI File Integrity ────────────────────────────────────
log_test "UI file integrity checks"

for file in index.html styles.css app.js; do
    if [ -f "${UI_DIR}/${file}" ]; then
        SIZE=$(wc -c < "${UI_DIR}/${file}")
        if [ "$SIZE" -gt 1000 ]; then
            pass "${file} exists (${SIZE} bytes)"
        else
            fail "${file} too small (${SIZE} bytes)"
        fi
    else
        fail "${file} missing"
    fi
done

# ─── Summary ──────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════"
echo -e " Results: ${GREEN}${PASS} passed${NC}, ${RED}${FAIL} failed${NC}, ${TOTAL} total"
echo "═══════════════════════════════════════════════════════"

if [ "$FAIL" -eq 0 ]; then
    echo -e " ${GREEN}✓ All tests passed!${NC}"
    exit 0
else
    echo -e " ${RED}✗ Some tests failed${NC}"
    exit 1
fi
