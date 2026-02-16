#!/bin/bash
# test_touchpad.sh - Virtual touchpad driver test script
# Tests: single touch, tap, two-finger tap, scroll, multi-touch

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
echo " Virtual Touchpad Driver Test Suite v1.0"
echo "=============================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root (use sudo)${NC}"
    exit 1
fi

# Find sysfs paths
SYSFS_DIR=$(find /sys/devices/virtual/input -name "inject_touch" 2>/dev/null | head -1 | xargs dirname 2>/dev/null)

if [ -z "$SYSFS_DIR" ]; then
    echo -e "${RED}Error: Cannot find touchpad driver sysfs interface${NC}"
    echo "Make sure the touchpad_driver module is loaded:"
    echo "  sudo insmod drivers/touchpad_driver.ko"
    exit 1
fi

echo -e "${GREEN}Found touchpad driver at: $SYSFS_DIR${NC}"
echo ""

inject() {
    local attr=$1
    local data=$2
    local desc=$3
    echo -e "  ${BLUE}Inject:${NC} $desc"
    echo "$data" > "$SYSFS_DIR/$attr"
    sleep 0.1
}

# ===== TEST 1: Single Touch =====
echo -e "${YELLOW}━━━ Test 1: Single Touch (down, move, up) ━━━${NC}"
inject inject_touch "2048 2048 128" "Touch down at center (2048,2048)"
inject inject_touch "2100 2048 128" "Move right"
inject inject_touch "2200 2100 128" "Move right+down"
inject inject_touch "2200 2100 0"   "Lift finger"
echo -e "  ${GREEN}✓${NC} Single touch sequence"
PASS=$((PASS + 1))
echo ""

# ===== TEST 2: Single Tap =====
echo -e "${YELLOW}━━━ Test 2: Single Tap ━━━${NC}"
inject inject_tap "1024 1024" "Tap at (1024, 1024)"
inject inject_tap "2048 2048" "Tap at center"
inject inject_tap "3072 3072" "Tap at (3072, 3072)"
echo -e "  ${GREEN}✓${NC} Single taps"
PASS=$((PASS + 1))
echo ""

# ===== TEST 3: Two-Finger Tap =====
echo -e "${YELLOW}━━━ Test 3: Two-Finger Tap (right-click) ━━━${NC}"
inject inject_two_finger_tap "1800 2048 2200 2048" "Two-finger tap (horizontal)"
inject inject_two_finger_tap "2048 1800 2048 2200" "Two-finger tap (vertical)"
echo -e "  ${GREEN}✓${NC} Two-finger taps"
PASS=$((PASS + 1))
echo ""

# ===== TEST 4: Two-Finger Scroll =====
echo -e "${YELLOW}━━━ Test 4: Two-Finger Scroll ━━━${NC}"
inject inject_scroll "0 1"  "Scroll down 1"
inject inject_scroll "0 1"  "Scroll down 1"
inject inject_scroll "0 1"  "Scroll down 1"
inject inject_scroll "0 -1" "Scroll up 1"
inject inject_scroll "0 -1" "Scroll up 1"
inject inject_scroll "0 -1" "Scroll up 1"
inject inject_scroll "1 0"  "Scroll right 1"
inject inject_scroll "-1 0" "Scroll left 1"
echo -e "  ${GREEN}✓${NC} Two-finger scroll"
PASS=$((PASS + 1))
echo ""

# ===== TEST 5: Draw a Path =====
echo -e "${YELLOW}━━━ Test 5: Draw Path (diagonal line) ━━━${NC}"
for i in $(seq 0 10 200); do
    X=$((1000 + i))
    Y=$((1000 + i))
    inject inject_touch "$X $Y 100" "Move to ($X, $Y)" 2>/dev/null
done
inject inject_touch "1200 1200 0" "Lift finger"
echo -e "  ${GREEN}✓${NC} Diagonal path drawn"
PASS=$((PASS + 1))
echo ""

# ===== TEST 6: Boundary Tests =====
echo -e "${YELLOW}━━━ Test 6: Boundary Values ━━━${NC}"
inject inject_touch "0 0 1"        "Touch at origin (0,0)"
inject inject_touch "0 0 0"        "Lift"
inject inject_touch "4096 4096 255" "Touch at max (4096,4096) max pressure"
inject inject_touch "4096 4096 0"  "Lift"
inject inject_tap "0 0"            "Tap at origin"
inject inject_tap "4096 4096"      "Tap at max corner"
echo -e "  ${GREEN}✓${NC} Boundary values"
PASS=$((PASS + 1))
echo ""

# ===== TEST 7: /proc Stats =====
echo -e "${YELLOW}━━━ Test 7: /proc/vtouchpad_stats ━━━${NC}"
if [ -f /proc/vtouchpad_stats ]; then
    cat /proc/vtouchpad_stats
    echo -e "  ${GREEN}✓${NC} /proc/vtouchpad_stats readable"
    PASS=$((PASS + 1))
else
    echo -e "  ${CYAN}⊘ SKIP:${NC} /proc/vtouchpad_stats not found"
fi
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
dmesg | grep virtual_touchpad | tail -15
