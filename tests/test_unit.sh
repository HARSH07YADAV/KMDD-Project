#!/bin/bash
# test_unit.sh - Unit tests for KMDD project
# Tests driver code logic without requiring module loading
# Validates: scan code tables, build system, file structure, binary checks

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
NC='\033[0m'

PASS=0
FAIL=0
SKIP=0
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

assert_pass() {
    PASS=$((PASS + 1))
    echo -e "  ${GREEN}✓ PASS:${NC} $1"
}

assert_fail() {
    FAIL=$((FAIL + 1))
    echo -e "  ${RED}✗ FAIL:${NC} $1"
}

assert_skip() {
    SKIP=$((SKIP + 1))
    echo -e "  ${CYAN}⊘ SKIP:${NC} $1"
}

echo "=============================================="
echo " KMDD Unit Test Suite v1.0"
echo "=============================================="
echo " Project: $SCRIPT_DIR"
echo ""

# ===== TEST 1: File Structure =====
echo -e "${YELLOW}━━━ Test 1: Project File Structure ━━━${NC}"

FILES=(
    "drivers/keyboard_driver.c"
    "drivers/mouse_driver.c"
    "drivers/touchpad_driver.c"
    "drivers/Kbuild"
    "userspace/reader.c"
    "userspace/event_logger.c"
    "tests/test_keyboard.sh"
    "tests/test_mouse.sh"
    "tests/test_touchpad.sh"
    "Makefile"
    "install.sh"
    "README.md"
)

for f in "${FILES[@]}"; do
    if [ -f "$SCRIPT_DIR/$f" ]; then
        assert_pass "File exists: $f"
    else
        assert_fail "File exists: $f"
    fi
done
echo ""

# ===== TEST 2: Keyboard Scan Code Coverage =====
echo -e "${YELLOW}━━━ Test 2: Keyboard Scan Code Table ━━━${NC}"

KBD="$SCRIPT_DIR/drivers/keyboard_driver.c"

# Check key groups exist in scan code table
for key in KEY_ESC KEY_F1 KEY_F11 KEY_F12 KEY_UP KEY_DOWN KEY_LEFT KEY_RIGHT \
           KEY_HOME KEY_END KEY_PAGEUP KEY_PAGEDOWN KEY_INSERT KEY_DELETE \
           KEY_KP0 KEY_KP9 KEY_KPPLUS KEY_KPMINUS KEY_NUMLOCK \
           KEY_MUTE KEY_VOLUMEUP KEY_VOLUMEDOWN KEY_PLAYPAUSE \
           KEY_LEFTMETA KEY_CAPSLOCK KEY_SCROLLLOCK; do
    if grep -q "$key" "$KBD" 2>/dev/null; then
        assert_pass "Scan code mapping: $key"
    else
        assert_fail "Scan code mapping: $key"
    fi
done
echo ""

# ===== TEST 3: Keyboard Features =====
echo -e "${YELLOW}━━━ Test 3: Keyboard Driver Features ━━━${NC}"

FEATURES_KBD=(
    "proc_create::/proc stats"
    "led_caps::LED Caps Lock sysfs"
    "led_num::LED Num Lock sysfs"
    "led_scroll::LED Scroll Lock sysfs"
    "repeat_delay::Repeat delay parameter"
    "repeat_rate::Repeat rate parameter"
    "check_combos::Combo detection"
    "buffer_overflow::Overflow tracking"
    "pr_warn_ratelimited::Rate-limited warnings"
    "EV_LED::LED event support"
)

for entry in "${FEATURES_KBD[@]}"; do
    pattern="${entry%%::*}"
    desc="${entry##*::}"
    if grep -q "$pattern" "$KBD" 2>/dev/null; then
        assert_pass "$desc"
    else
        assert_fail "$desc"
    fi
done
echo ""

# ===== TEST 4: Mouse Driver Features =====
echo -e "${YELLOW}━━━ Test 4: Mouse Driver Features ━━━${NC}"

MOUSE="$SCRIPT_DIR/drivers/mouse_driver.c"

FEATURES_MOUSE=(
    "REL_WHEEL::Scroll wheel support"
    "REL_HWHEEL::Horizontal scroll"
    "BTN_SIDE::Side button"
    "BTN_EXTRA::Forward button"
    "dpi_multiplier::DPI multiplier"
    "intellimouse_mode::IntelliMouse mode toggle"
    "proc_create::proc stats"
    "PACKET_SIZE_INTELLIMOUSE::4-byte packet support"
    "apply_dpi::DPI scaling function"
    "total_distance::Distance tracking"
)

for entry in "${FEATURES_MOUSE[@]}"; do
    pattern="${entry%%::*}"
    desc="${entry##*::}"
    if grep -q "$pattern" "$MOUSE" 2>/dev/null; then
        assert_pass "$desc"
    else
        assert_fail "$desc"
    fi
done
echo ""

# ===== TEST 5: Touchpad Driver Features =====
echo -e "${YELLOW}━━━ Test 5: Touchpad Driver Features ━━━${NC}"

TOUCHPAD="$SCRIPT_DIR/drivers/touchpad_driver.c"

FEATURES_TP=(
    "ABS_MT_POSITION_X::Multi-touch X axis"
    "ABS_MT_POSITION_Y::Multi-touch Y axis"
    "ABS_MT_PRESSURE::Multi-touch pressure"
    "input_mt_init_slots::MT slot initialization"
    "input_mt_sync_frame::MT frame sync"
    "inject_tap::Tap injection"
    "inject_two_finger_tap::Two-finger tap"
    "inject_scroll::Scroll injection"
    "BTN_TOOL_FINGER::Finger tool"
    "BTN_TOOL_DOUBLETAP::Double-tap tool"
    "proc_create::proc stats"
)

for entry in "${FEATURES_TP[@]}"; do
    pattern="${entry%%::*}"
    desc="${entry##*::}"
    if grep -q "$pattern" "$TOUCHPAD" 2>/dev/null; then
        assert_pass "$desc"
    else
        assert_fail "$desc"
    fi
done
echo ""

# ===== TEST 6: Event Logger Features =====
echo -e "${YELLOW}━━━ Test 6: Event Logger Features ━━━${NC}"

LOGGER="$SCRIPT_DIR/userspace/event_logger.c"

FEATURES_LOG=(
    "rotate_log::Log rotation"
    "should_log::Event filtering"
    "write_json_event::JSON output"
    "iso_timestamp::ISO timestamps"
    "daemon_mode::Daemon mode"
    "MAX_ROTATIONS::Rotation limit"
    "getopt::CLI argument parsing"
)

for entry in "${FEATURES_LOG[@]}"; do
    pattern="${entry%%::*}"
    desc="${entry##*::}"
    if grep -q "$pattern" "$LOGGER" 2>/dev/null; then
        assert_pass "$desc"
    else
        assert_fail "$desc"
    fi
done
echo ""

# ===== TEST 7: Build Artifacts =====
echo -e "${YELLOW}━━━ Test 7: Build Artifacts ━━━${NC}"

ARTIFACTS=(
    "drivers/keyboard_driver.ko::Keyboard module"
    "drivers/mouse_driver.ko::Mouse module"
    "drivers/touchpad_driver.ko::Touchpad module"
    "userspace/reader::Reader binary"
    "userspace/event_logger::Logger binary"
)

for entry in "${ARTIFACTS[@]}"; do
    path="${entry%%::*}"
    desc="${entry##*::}"
    if [ -f "$SCRIPT_DIR/$path" ]; then
        SIZE=$(du -h "$SCRIPT_DIR/$path" | awk '{print $1}')
        assert_pass "$desc ($SIZE)"
    else
        assert_fail "$desc not built"
    fi
done
echo ""

# ===== TEST 8: Kbuild Configuration =====
echo -e "${YELLOW}━━━ Test 8: Kbuild Configuration ━━━${NC}"

KBUILD="$SCRIPT_DIR/drivers/Kbuild"
for mod in keyboard_driver mouse_driver touchpad_driver; do
    if grep -q "$mod" "$KBUILD" 2>/dev/null; then
        assert_pass "Kbuild includes $mod"
    else
        assert_fail "Kbuild includes $mod"
    fi
done
echo ""

# ===== TEST 9: Module Info =====
echo -e "${YELLOW}━━━ Test 9: Module Metadata ━━━${NC}"

for ko in keyboard_driver mouse_driver touchpad_driver; do
    KO_FILE="$SCRIPT_DIR/drivers/${ko}.ko"
    if [ -f "$KO_FILE" ]; then
        VER=$(modinfo "$KO_FILE" 2>/dev/null | grep "^version:" | awk '{print $2}')
        DESC=$(modinfo "$KO_FILE" 2>/dev/null | grep "^description:" | cut -d: -f2-)
        if [ -n "$VER" ]; then
            assert_pass "$ko: version $VER"
        else
            assert_fail "$ko: no version info"
        fi
    else
        assert_skip "$ko: .ko file not built"
    fi
done
echo ""

# ===== TEST 10: Reader Binary Test =====
echo -e "${YELLOW}━━━ Test 10: Reader Binary Validation ━━━${NC}"
READER="$SCRIPT_DIR/userspace/reader"
if [ -f "$READER" ]; then
    # Check it runs without args (should return 1 with usage)
    $READER 2>/dev/null && assert_fail "Reader should fail without args" || assert_pass "Reader usage message on no args"
    
    # Check --json flag is documented
    if $READER 2>&1 | grep -q "json"; then
        assert_pass "Reader supports --json flag"
    else
        assert_fail "Reader --json flag not documented"
    fi
else
    assert_skip "Reader binary not built"
fi
echo ""

# ===== TEST 11: Logger Binary Test =====
echo -e "${YELLOW}━━━ Test 11: Logger Binary Validation ━━━${NC}"
LOGGER_BIN="$SCRIPT_DIR/userspace/event_logger"
if [ -f "$LOGGER_BIN" ]; then
    # Check help flag
    if $LOGGER_BIN -h 2>&1 | grep -q "Event Logger"; then
        assert_pass "Logger help message"
    else
        assert_fail "Logger help message"
    fi

    # Check filter options are documented
    if $LOGGER_BIN -h 2>&1 | grep -q "keyboard"; then
        assert_pass "Logger filter options documented"
    else
        assert_fail "Logger filter options"
    fi
else
    assert_skip "Logger binary not built"
fi
echo ""

# ===== TEST 12: Web Dashboard Files =====
echo -e "${YELLOW}━━━ Test 12: Web Dashboard Files ━━━${NC}"

DASHBOARD_FILES=(
    "backend/server.py::Backend server"
    "ui/index.html::Dashboard HTML"
    "ui/styles.css::Dashboard CSS"
    "ui/app.js::Dashboard JS"
)

for entry in "${DASHBOARD_FILES[@]}"; do
    path="${entry%%::*}"
    desc="${entry##*::}"
    if [ -f "$SCRIPT_DIR/$path" ]; then
        assert_pass "$desc exists"
    else
        assert_fail "$desc exists"
    fi
done

# Check server.py features
SERVER="$SCRIPT_DIR/backend/server.py"
if [ -f "$SERVER" ]; then
    if grep -q "websockets" "$SERVER" 2>/dev/null; then
        assert_pass "Server uses websockets"
    else
        assert_fail "Server uses websockets"
    fi
    if grep -q "simulate" "$SERVER" 2>/dev/null; then
        assert_pass "Server has simulation mode"
    else
        assert_fail "Server has simulation mode"
    fi
fi

# Check index.html features
HTML="$SCRIPT_DIR/ui/index.html"
if [ -f "$HTML" ]; then
    for section in "live-events" "keyboard" "mouse" "statistics" "settings"; do
        if grep -q "$section" "$HTML" 2>/dev/null; then
            assert_pass "Dashboard tab: $section"
        else
            assert_fail "Dashboard tab: $section"
        fi
    done
fi
echo ""

# ===== Summary =====
echo "=============================================="
echo -e " ${CYAN}Unit Test Summary${NC}"
echo "=============================================="
echo -e "  ${GREEN}Passed:  $PASS${NC}"
echo -e "  ${RED}Failed:  $FAIL${NC}"
echo -e "  ${CYAN}Skipped: $SKIP${NC}"
TOTAL=$((PASS + FAIL))
echo -e "  Total:   $TOTAL"
echo ""

if [ $FAIL -eq 0 ]; then
    echo -e "  ${GREEN}▶ ALL TESTS PASSED ✓${NC}"
    exit 0
else
    echo -e "  ${RED}▶ $FAIL TEST(S) FAILED ✗${NC}"
    exit 1
fi
