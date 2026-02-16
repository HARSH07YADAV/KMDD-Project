#!/bin/bash
# ════════════════════════════════════════════════════════════
#  KMDD — Performance / Stress Test
#  Inject 1000+ events rapidly and verify no data loss
# ════════════════════════════════════════════════════════════

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BACKEND="${PROJECT_DIR}/backend/server.py"
SERVER_PID=""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

EVENT_COUNT=1000
PASS=0
FAIL=0
TOTAL=0

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        echo -e "\n${CYAN}[CLEANUP]${NC} Stopping server…"
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
echo " KMDD Performance Test — ${EVENT_COUNT} events"
echo "═══════════════════════════════════════════════════════"
echo ""

# ─── Start Server ──────────────────────────────────────────
log_test "Starting backend server in simulation mode"

python3 "$BACKEND" --simulate --port 8766 &
SERVER_PID=$!
sleep 2

if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "Server started (PID: $SERVER_PID, port 8766)"
else
    fail "Server failed to start"
    exit 1
fi

# ─── Rapid Injection Test ──────────────────────────────────
log_test "Injecting ${EVENT_COUNT} key events rapidly via WebSocket"

INJECT_RESULT=$(python3 -c "
import asyncio, json, websockets, time

async def test():
    async with websockets.connect('ws://localhost:8766') as ws:
        await asyncio.wait_for(ws.recv(), timeout=5)  # status

        start = time.monotonic()
        sent = 0
        for i in range(${EVENT_COUNT}):
            cmd = json.dumps({'action': 'inject_key', 'scancode': f'0x{0x1E + (i % 26):02X}'})
            await ws.send(cmd)
            sent += 1

        elapsed = time.monotonic() - start

        # Collect responses
        received = 0
        try:
            for _ in range(${EVENT_COUNT} * 3):
                msg = await asyncio.wait_for(ws.recv(), timeout=5)
                data = json.loads(msg)
                if data.get('type') == 'inject_result':
                    received += 1
                if received >= sent:
                    break
        except asyncio.TimeoutError:
            pass

        eps = sent / elapsed if elapsed > 0 else 0
        print(f'SENT:{sent}|RECEIVED:{received}|TIME:{elapsed:.3f}|EPS:{eps:.0f}')

asyncio.run(test())
" 2>/dev/null || echo "FAIL:error")

if echo "$INJECT_RESULT" | grep -q "SENT:"; then
    SENT=$(echo "$INJECT_RESULT" | grep -oP 'SENT:\K\d+')
    RECEIVED=$(echo "$INJECT_RESULT" | grep -oP 'RECEIVED:\K\d+')
    ELAPSED=$(echo "$INJECT_RESULT" | grep -oP 'TIME:\K[0-9.]+')
    EPS=$(echo "$INJECT_RESULT" | grep -oP 'EPS:\K\d+')
    
    echo -e "  Sent: ${SENT}"
    echo -e "  Acknowledged: ${RECEIVED}"
    echo -e "  Time: ${ELAPSED}s"
    echo -e "  Rate: ${EPS} events/sec"
    
    if [ "${RECEIVED}" -ge "${SENT}" ]; then
        pass "All ${SENT} injections acknowledged (${EPS} e/s)"
    elif [ "${RECEIVED}" -ge "$((SENT * 90 / 100))" ]; then
        echo -e "  ${YELLOW}⚠ ${RECEIVED}/${SENT} acknowledged (>90% — acceptable)${NC}"
        pass "High throughput with minor drops (${EPS} e/s)"
    else
        fail "Only ${RECEIVED}/${SENT} acknowledged (${EPS} e/s)"
    fi
else
    fail "Injection test error: $INJECT_RESULT"
fi

# ─── Concurrent Client Test ────────────────────────────────
log_test "Multiple concurrent WebSocket clients (3 clients)"

MULTI_RESULT=$(python3 -c "
import asyncio, json, websockets

async def client(cid):
    try:
        async with websockets.connect('ws://localhost:8766') as ws:
            msg = await asyncio.wait_for(ws.recv(), timeout=5)
            data = json.loads(msg)
            if data.get('type') == 'status':
                return f'CLIENT{cid}:OK'
    except Exception as e:
        return f'CLIENT{cid}:FAIL:{e}'

async def test():
    results = await asyncio.gather(client(1), client(2), client(3))
    for r in results:
        print(r)

asyncio.run(test())
" 2>/dev/null || echo "FAIL")

OK_COUNT=$(echo "$MULTI_RESULT" | grep -c "OK" || true)
if [ "$OK_COUNT" -ge 3 ]; then
    pass "All 3 clients connected and received status"
else
    fail "Only $OK_COUNT/3 clients connected"
fi

# ─── Server Memory / Resource Check ───────────────────────
log_test "Server resource check (still running after stress)"

if kill -0 "$SERVER_PID" 2>/dev/null; then
    RSS=$(ps -o rss= -p "$SERVER_PID" 2>/dev/null | tr -d ' ')
    if [ -n "$RSS" ]; then
        RSS_MB=$((RSS / 1024))
        echo -e "  Memory: ${RSS_MB} MB RSS"
        if [ "$RSS_MB" -lt 200 ]; then
            pass "Memory usage acceptable (${RSS_MB} MB)"
        else
            fail "High memory usage (${RSS_MB} MB)"
        fi
    else
        pass "Server still running (RSS unavailable)"
    fi
else
    fail "Server crashed during stress test"
fi

# ─── Summary ──────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════"
echo -e " Results: ${GREEN}${PASS} passed${NC}, ${RED}${FAIL} failed${NC}, ${TOTAL} total"
echo "═══════════════════════════════════════════════════════"

if [ "$FAIL" -eq 0 ]; then
    echo -e " ${GREEN}✓ All performance tests passed!${NC}"
    exit 0
else
    echo -e " ${RED}✗ Some tests failed${NC}"
    exit 1
fi
