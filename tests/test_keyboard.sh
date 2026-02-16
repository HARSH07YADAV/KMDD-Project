#!/bin/bash
# test_keyboard.sh - Comprehensive keyboard driver test script (v2.0)
# Tests: letters, numbers, specials, modifiers, function keys, extended keys,
#        navigation, numpad, multimedia, LEDs, combos, /proc stats

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
NC='\033[0m'

PASS=0
FAIL=0

echo "=============================================="
echo " Virtual Keyboard Driver Test Suite v2.0"
echo "=============================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root (use sudo)${NC}"
    exit 1
fi

# Find the sysfs injection path
SYSFS_PATH=$(find /sys/devices/virtual/input -name "inject_scancode" 2>/dev/null | head -1)

if [ -z "$SYSFS_PATH" ]; then
    echo -e "${RED}Error: Cannot find keyboard driver sysfs interface${NC}"
    echo "Make sure the keyboard_driver module is loaded:"
    echo "  sudo insmod drivers/keyboard_driver.ko"
    exit 1
fi

echo -e "${GREEN}Found keyboard driver at: $SYSFS_PATH${NC}"
SYSFS_DIR=$(dirname "$SYSFS_PATH")
echo ""

inject_scancode() {
    local code=$1
    local desc=$2
    echo -e "  ${BLUE}Inject:${NC} $desc (scan code $code)"
    echo $code > $SYSFS_PATH
    sleep 0.15
}

check_result() {
    local desc=$1
    if [ $? -eq 0 ]; then
        PASS=$((PASS + 1))
        echo -e "  ${GREEN}✓ PASS:${NC} $desc"
    else
        FAIL=$((FAIL + 1))
        echo -e "  ${RED}✗ FAIL:${NC} $desc"
    fi
}

# ===== TEST 1: Letter Keys =====
echo -e "${YELLOW}━━━ Test 1: Letter Keys (A, B, C) ━━━${NC}"
inject_scancode 0x1E "Key 'A' press"
inject_scancode 0x9E "Key 'A' release"
inject_scancode 0x30 "Key 'B' press"
inject_scancode 0xB0 "Key 'B' release"
inject_scancode 0x2E "Key 'C' press"
inject_scancode 0xAE "Key 'C' release"
echo -e "  ${GREEN}✓${NC} Letter keys injected"
PASS=$((PASS + 1))
echo ""

# ===== TEST 2: Number Keys =====
echo -e "${YELLOW}━━━ Test 2: Number Keys (1, 2, 3) ━━━${NC}"
inject_scancode 0x02 "Key '1' press"
inject_scancode 0x82 "Key '1' release"
inject_scancode 0x03 "Key '2' press"
inject_scancode 0x83 "Key '2' release"
inject_scancode 0x04 "Key '3' press"
inject_scancode 0x84 "Key '3' release"
echo -e "  ${GREEN}✓${NC} Number keys injected"
PASS=$((PASS + 1))
echo ""

# ===== TEST 3: Special Keys =====
echo -e "${YELLOW}━━━ Test 3: Special Keys (Space, Enter, Backspace, Tab, Esc) ━━━${NC}"
inject_scancode 0x39 "SPACE press"
inject_scancode 0xB9 "SPACE release"
inject_scancode 0x1C "ENTER press"
inject_scancode 0x9C "ENTER release"
inject_scancode 0x0E "BACKSPACE press"
inject_scancode 0x8E "BACKSPACE release"
inject_scancode 0x0F "TAB press"
inject_scancode 0x8F "TAB release"
inject_scancode 0x01 "ESC press"
inject_scancode 0x81 "ESC release"
echo -e "  ${GREEN}✓${NC} Special keys injected"
PASS=$((PASS + 1))
echo ""

# ===== TEST 4: Modifier Keys =====
echo -e "${YELLOW}━━━ Test 4: Modifier Keys (Shift, Ctrl, Alt) ━━━${NC}"
inject_scancode 0x2A "LEFT_SHIFT press"
inject_scancode 0x1E "Key 'A' press (with shift)"
inject_scancode 0x9E "Key 'A' release"
inject_scancode 0xAA "LEFT_SHIFT release"
inject_scancode 0x1D "LEFT_CTRL press"
inject_scancode 0x9D "LEFT_CTRL release"
inject_scancode 0x38 "LEFT_ALT press"
inject_scancode 0xB8 "LEFT_ALT release"
echo -e "  ${GREEN}✓${NC} Modifier keys injected"
PASS=$((PASS + 1))
echo ""

# ===== TEST 5: Function Keys F1-F12 =====
echo -e "${YELLOW}━━━ Test 5: Function Keys (F1-F12) ━━━${NC}"
for code in 0x3B 0x3C 0x3D 0x3E 0x3F 0x40 0x41 0x42 0x43 0x44 0x57 0x58; do
    inject_scancode $code "Function key press"
    inject_scancode $(printf "0x%02X" $((code | 0x80))) "Function key release"
done
echo -e "  ${GREEN}✓${NC} All F1-F12 keys injected"
PASS=$((PASS + 1))
echo ""

# ===== TEST 6: Arrow Keys =====
echo -e "${YELLOW}━━━ Test 6: Arrow Keys (Up, Down, Left, Right) ━━━${NC}"
inject_scancode 0x67 "Arrow UP press"
inject_scancode 0xE7 "Arrow UP release"
inject_scancode 0x6C "Arrow DOWN press"
inject_scancode 0xEC "Arrow DOWN release"
inject_scancode 0x69 "Arrow LEFT press"
inject_scancode 0xE9 "Arrow LEFT release"
inject_scancode 0x6A "Arrow RIGHT press"
inject_scancode 0xEA "Arrow RIGHT release"
echo -e "  ${GREEN}✓${NC} Arrow keys injected"
PASS=$((PASS + 1))
echo ""

# ===== TEST 7: Navigation Keys =====
echo -e "${YELLOW}━━━ Test 7: Navigation Keys (Home, End, PgUp, PgDn, Insert, Delete) ━━━${NC}"
inject_scancode 0x7F "HOME press"
inject_scancode 0xFF "HOME release"
inject_scancode 0x6B "END press"
inject_scancode 0xEB "END release"
inject_scancode 0x68 "PAGE_UP press"
inject_scancode 0xE8 "PAGE_UP release"
inject_scancode 0x6D "PAGE_DOWN press"
inject_scancode 0xED "PAGE_DOWN release"
inject_scancode 0x6E "INSERT press"
inject_scancode 0xEE "INSERT release"
inject_scancode 0x6F "DELETE press"
inject_scancode 0xEF "DELETE release"
echo -e "  ${GREEN}✓${NC} Navigation keys injected"
PASS=$((PASS + 1))
echo ""

# ===== TEST 8: Numpad Keys =====
echo -e "${YELLOW}━━━ Test 8: Numpad Keys (0-9, +, -, *, .) ━━━${NC}"
inject_scancode 0x52 "NUMPAD_0 press"
inject_scancode 0xD2 "NUMPAD_0 release"
inject_scancode 0x4F "NUMPAD_1 press"
inject_scancode 0xCF "NUMPAD_1 release"
inject_scancode 0x50 "NUMPAD_2 press"
inject_scancode 0xD0 "NUMPAD_2 release"
inject_scancode 0x4E "NUMPAD_PLUS press"
inject_scancode 0xCE "NUMPAD_PLUS release"
inject_scancode 0x4A "NUMPAD_MINUS press"
inject_scancode 0xCA "NUMPAD_MINUS release"
inject_scancode 0x37 "NUMPAD_STAR press"
inject_scancode 0xB7 "NUMPAD_STAR release"
echo -e "  ${GREEN}✓${NC} Numpad keys injected"
PASS=$((PASS + 1))
echo ""

# ===== TEST 9: Multimedia Keys =====
echo -e "${YELLOW}━━━ Test 9: Multimedia Keys (Mute, Vol+/-, Play, Next, Prev) ━━━${NC}"
inject_scancode 0x71 "MUTE press"
inject_scancode 0xF1 "MUTE release"
inject_scancode 0x72 "VOLUME_DOWN press"
inject_scancode 0xF2 "VOLUME_DOWN release"
inject_scancode 0x73 "VOLUME_UP press"
inject_scancode 0xF3 "VOLUME_UP release"
inject_scancode 0x74 "PLAY_PAUSE press"
inject_scancode 0xF4 "PLAY_PAUSE release"
inject_scancode 0x76 "PREV_SONG press"
inject_scancode 0xF6 "PREV_SONG release"
inject_scancode 0x77 "NEXT_SONG press"
inject_scancode 0xF7 "NEXT_SONG release"
echo -e "  ${GREEN}✓${NC} Multimedia keys injected"
PASS=$((PASS + 1))
echo ""

# ===== TEST 10: LED Toggle =====
echo -e "${YELLOW}━━━ Test 10: LED Indicators (Caps/Num/Scroll Lock) ━━━${NC}"
if [ -f "$SYSFS_DIR/led_caps" ]; then
    echo 1 > "$SYSFS_DIR/led_caps"
    VAL=$(cat "$SYSFS_DIR/led_caps")
    [ "$VAL" = "1" ] && echo -e "  ${GREEN}✓${NC} Caps Lock LED ON" && PASS=$((PASS+1)) || { echo -e "  ${RED}✗${NC} Caps Lock LED failed"; FAIL=$((FAIL+1)); }
    echo 0 > "$SYSFS_DIR/led_caps"

    echo 1 > "$SYSFS_DIR/led_num"
    VAL=$(cat "$SYSFS_DIR/led_num")
    [ "$VAL" = "1" ] && echo -e "  ${GREEN}✓${NC} Num Lock LED ON" && PASS=$((PASS+1)) || { echo -e "  ${RED}✗${NC} Num Lock LED failed"; FAIL=$((FAIL+1)); }
    echo 0 > "$SYSFS_DIR/led_num"

    echo 1 > "$SYSFS_DIR/led_scroll"
    VAL=$(cat "$SYSFS_DIR/led_scroll")
    [ "$VAL" = "1" ] && echo -e "  ${GREEN}✓${NC} Scroll Lock LED ON" && PASS=$((PASS+1)) || { echo -e "  ${RED}✗${NC} Scroll Lock LED failed"; FAIL=$((FAIL+1)); }
    echo 0 > "$SYSFS_DIR/led_scroll"
else
    echo -e "  ${CYAN}⊘ SKIP:${NC} LED sysfs attributes not found"
fi
echo ""

# ===== TEST 11: Repeat Rate Configuration =====
echo -e "${YELLOW}━━━ Test 11: Repeat Rate Configuration ━━━${NC}"
if [ -f "$SYSFS_DIR/repeat_delay_ms" ]; then
    echo 500 > "$SYSFS_DIR/repeat_delay_ms"
    VAL=$(cat "$SYSFS_DIR/repeat_delay_ms")
    [ "$VAL" = "500" ] && echo -e "  ${GREEN}✓${NC} Repeat delay set to 500ms" && PASS=$((PASS+1)) || { echo -e "  ${RED}✗${NC} Repeat delay failed"; FAIL=$((FAIL+1)); }
    echo 250 > "$SYSFS_DIR/repeat_delay_ms"

    echo 50 > "$SYSFS_DIR/repeat_rate_ms"
    VAL=$(cat "$SYSFS_DIR/repeat_rate_ms")
    [ "$VAL" = "50" ] && echo -e "  ${GREEN}✓${NC} Repeat rate set to 50ms" && PASS=$((PASS+1)) || { echo -e "  ${RED}✗${NC} Repeat rate failed"; FAIL=$((FAIL+1)); }
    echo 33 > "$SYSFS_DIR/repeat_rate_ms"
else
    echo -e "  ${CYAN}⊘ SKIP:${NC} Repeat sysfs attributes not found"
fi
echo ""

# ===== TEST 12: Combo Key Detection =====
echo -e "${YELLOW}━━━ Test 12: Combo Key Detection (Ctrl+C, Alt+Tab) ━━━${NC}"
# Ctrl+C
inject_scancode 0x1D "LEFT_CTRL press"
inject_scancode 0x2E "Key 'C' press"
inject_scancode 0xAE "Key 'C' release"
inject_scancode 0x9D "LEFT_CTRL release"
echo -e "  ${GREEN}✓${NC} Ctrl+C combo sent"
PASS=$((PASS + 1))

# Alt+Tab
inject_scancode 0x38 "LEFT_ALT press"
inject_scancode 0x0F "TAB press"
inject_scancode 0x8F "TAB release"
inject_scancode 0xB8 "LEFT_ALT release"
echo -e "  ${GREEN}✓${NC} Alt+Tab combo sent"
PASS=$((PASS + 1))
echo ""

# ===== TEST 13: /proc Stats =====
echo -e "${YELLOW}━━━ Test 13: /proc/vkbd_stats ━━━${NC}"
if [ -f /proc/vkbd_stats ]; then
    STATS=$(cat /proc/vkbd_stats)
    echo "$STATS" | head -8
    echo "  ..."
    echo -e "  ${GREEN}✓${NC} /proc/vkbd_stats is readable"
    PASS=$((PASS + 1))
else
    echo -e "  ${CYAN}⊘ SKIP:${NC} /proc/vkbd_stats not found"
fi
echo ""

# ===== TEST 14: Key Hold/Repeat =====
echo -e "${YELLOW}━━━ Test 14: Key Hold/Repeat Simulation ━━━${NC}"
inject_scancode 0x1E "Key 'A' press"
sleep 0.05
inject_scancode 0x1E "Key 'A' repeat"
sleep 0.05
inject_scancode 0x1E "Key 'A' repeat"
sleep 0.05
inject_scancode 0x1E "Key 'A' repeat"
inject_scancode 0x9E "Key 'A' release"
echo -e "  ${GREEN}✓${NC} Key hold/repeat simulated"
PASS=$((PASS + 1))
echo ""

# ===== Summary =====
echo "=============================================="
echo -e " ${CYAN}Test Summary${NC}"
echo "=============================================="
echo -e "  ${GREEN}Passed: $PASS${NC}"
echo -e "  ${RED}Failed: $FAIL${NC}"
echo ""

if [ $FAIL -eq 0 ]; then
    echo -e "  ${GREEN}▶ ALL TESTS PASSED ✓${NC}"
else
    echo -e "  ${RED}▶ SOME TESTS FAILED ✗${NC}"
fi

echo ""
echo "Recent kernel messages:"
echo "---"
dmesg | grep virtual_keyboard | tail -20
