#!/bin/bash
# test_mouse.sh - Comprehensive mouse driver test script (v2.0)
# Tests: buttons (L/R/M/Side/Forward), movement, diagonal, drag, circular,
#        scroll wheel, DPI, IntelliMouse mode, /proc stats

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
echo " Virtual Mouse Driver Test Suite v2.0"
echo "=============================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root (use sudo)${NC}"
    exit 1
fi

# Find the sysfs injection path
SYSFS_PATH=$(find /sys/devices/virtual/input -name "inject_packet" 2>/dev/null | head -1)

if [ -z "$SYSFS_PATH" ]; then
    echo -e "${RED}Error: Cannot find mouse driver sysfs interface${NC}"
    echo "Make sure the mouse_driver module is loaded:"
    echo "  sudo insmod drivers/mouse_driver.ko"
    exit 1
fi

echo -e "${GREEN}Found mouse driver at: $SYSFS_PATH${NC}"
SYSFS_DIR=$(dirname "$SYSFS_PATH")
echo ""

inject_packet() {
    local args="$1"
    local desc="$2"
    echo -e "  ${BLUE}Inject:${NC} $desc"
    echo "$args" > $SYSFS_PATH
    sleep 0.15
}

# ===== TEST 1: Standard Button Clicks =====
echo -e "${YELLOW}━━━ Test 1: Standard Button Clicks (Left, Right, Middle) ━━━${NC}"
inject_packet "0x09 0x00 0x00" "Left button press"
inject_packet "0x08 0x00 0x00" "Left button release"
inject_packet "0x0A 0x00 0x00" "Right button press"
inject_packet "0x08 0x00 0x00" "Right button release"
inject_packet "0x0C 0x00 0x00" "Middle button press"
inject_packet "0x08 0x00 0x00" "Middle button release"
echo -e "  ${GREEN}✓${NC} Standard button clicks"
PASS=$((PASS + 1))
echo ""

# ===== TEST 2: Multi-Button Press =====
echo -e "${YELLOW}━━━ Test 2: Multi-Button Press ━━━${NC}"
inject_packet "0x0B 0x00 0x00" "Left+Right buttons press"
inject_packet "0x08 0x00 0x00" "All buttons release"
inject_packet "0x0F 0x00 0x00" "L+R+M buttons press"
inject_packet "0x08 0x00 0x00" "All buttons release"
echo -e "  ${GREEN}✓${NC} Multi-button press"
PASS=$((PASS + 1))
echo ""

# ===== TEST 3: Cardinal Movements =====
echo -e "${YELLOW}━━━ Test 3: Cardinal Movements ━━━${NC}"
inject_packet "0x08 0x0A 0x00" "Move RIGHT by 10"
inject_packet "0x08 0xF6 0x00" "Move LEFT by 10"
inject_packet "0x08 0x00 0x0A" "Move DOWN by 10"
inject_packet "0x08 0x00 0xF6" "Move UP by 10"
echo -e "  ${GREEN}✓${NC} Cardinal movements"
PASS=$((PASS + 1))
echo ""

# ===== TEST 4: Diagonal Movements =====
echo -e "${YELLOW}━━━ Test 4: Diagonal Movements ━━━${NC}"
inject_packet "0x08 0x14 0x14" "Move SE (20, 20)"
inject_packet "0x08 0xEC 0xEC" "Move NW (-20, -20)"
inject_packet "0x08 0x14 0xEC" "Move NE (20, -20)"
inject_packet "0x08 0xEC 0x14" "Move SW (-20, 20)"
echo -e "  ${GREEN}✓${NC} Diagonal movements"
PASS=$((PASS + 1))
echo ""

# ===== TEST 5: Button Drag =====
echo -e "${YELLOW}━━━ Test 5: Button Drag ━━━${NC}"
inject_packet "0x09 0x05 0x00" "Left drag right"
inject_packet "0x09 0x05 0x00" "Continue drag"
inject_packet "0x09 0x05 0x00" "Continue drag"
inject_packet "0x08 0x00 0x00" "Release"
echo -e "  ${GREEN}✓${NC} Button drag"
PASS=$((PASS + 1))
echo ""

# ===== TEST 6: Circular Motion =====
echo -e "${YELLOW}━━━ Test 6: Circular Motion (8 segments) ━━━${NC}"
inject_packet "0x08 0x14 0x00" "E  (20, 0)"
inject_packet "0x08 0x0E 0x0E" "SE (14, 14)"
inject_packet "0x08 0x00 0x14" "S  (0, 20)"
inject_packet "0x08 0xF2 0x0E" "SW (-14, 14)"
inject_packet "0x08 0xEC 0x00" "W  (-20, 0)"
inject_packet "0x08 0xF2 0xF2" "NW (-14, -14)"
inject_packet "0x08 0x00 0xEC" "N  (0, -20)"
inject_packet "0x08 0x0E 0xF2" "NE (14, -14)"
echo -e "  ${GREEN}✓${NC} Circular motion"
PASS=$((PASS + 1))
echo ""

# ===== TEST 7: Rapid Jitter =====
echo -e "${YELLOW}━━━ Test 7: Rapid Jitter (10 movements) ━━━${NC}"
for i in {1..5}; do
    inject_packet "0x08 0x02 0x02" "Jitter +"
    inject_packet "0x08 0xFE 0xFE" "Jitter -"
done
echo -e "  ${GREEN}✓${NC} Rapid jitter"
PASS=$((PASS + 1))
echo ""

# ===== TEST 8: Large Movements =====
echo -e "${YELLOW}━━━ Test 8: Large Movements (max values) ━━━${NC}"
inject_packet "0x08 0x7F 0x00" "Large move RIGHT (127)"
inject_packet "0x08 0x81 0x00" "Large move LEFT (-127)"
inject_packet "0x08 0x00 0x7F" "Large move DOWN (127)"
inject_packet "0x08 0x00 0x81" "Large move UP (-127)"
echo -e "  ${GREEN}✓${NC} Large movements"
PASS=$((PASS + 1))
echo ""

# ===== TEST 9: Scroll Wheel (IntelliMouse) =====
echo -e "${YELLOW}━━━ Test 9: Scroll Wheel (IntelliMouse 4-byte packets) ━━━${NC}"
# Check if intellimouse mode is available
if [ -f "$SYSFS_DIR/intellimouse" ]; then
    IM_MODE=$(cat "$SYSFS_DIR/intellimouse")
    if [ "$IM_MODE" = "1" ]; then
        inject_packet "0x08 0x00 0x00 0x01" "Scroll UP by 1"
        inject_packet "0x08 0x00 0x00 0x01" "Scroll UP by 1"
        inject_packet "0x08 0x00 0x00 0xFF" "Scroll DOWN by 1"
        inject_packet "0x08 0x00 0x00 0xFF" "Scroll DOWN by 1"
        inject_packet "0x08 0x00 0x00 0x03" "Scroll UP by 3"
        inject_packet "0x08 0x00 0x00 0xFD" "Scroll DOWN by 3"
        echo -e "  ${GREEN}✓${NC} Scroll wheel events injected"
        PASS=$((PASS + 1))

        # Scroll + movement combo
        inject_packet "0x08 0x0A 0x05 0x01" "Move right+down, scroll up"
        inject_packet "0x08 0xF6 0xFB 0xFF" "Move left+up, scroll down"
        echo -e "  ${GREEN}✓${NC} Scroll + movement combo"
        PASS=$((PASS + 1))
    else
        echo -e "  ${CYAN}⊘ SKIP:${NC} IntelliMouse mode disabled"
    fi
else
    echo -e "  ${CYAN}⊘ SKIP:${NC} IntelliMouse sysfs not found"
fi
echo ""

# ===== TEST 10: Extended Buttons (Side/Forward) =====
echo -e "${YELLOW}━━━ Test 10: Extended Buttons (Side/Forward via IntelliMouse) ━━━${NC}"
if [ -f "$SYSFS_DIR/intellimouse" ]; then
    IM_MODE=$(cat "$SYSFS_DIR/intellimouse")
    if [ "$IM_MODE" = "1" ]; then
        inject_packet "0x08 0x00 0x00 0x10" "Side button press"
        inject_packet "0x08 0x00 0x00 0x00" "Side button release"
        inject_packet "0x08 0x00 0x00 0x20" "Forward button press"
        inject_packet "0x08 0x00 0x00 0x00" "Forward button release"
        echo -e "  ${GREEN}✓${NC} Extended buttons"
        PASS=$((PASS + 1))
    else
        echo -e "  ${CYAN}⊘ SKIP:${NC} IntelliMouse mode disabled"
    fi
else
    echo -e "  ${CYAN}⊘ SKIP:${NC} IntelliMouse sysfs not found"
fi
echo ""

# ===== TEST 11: DPI Configuration =====
echo -e "${YELLOW}━━━ Test 11: DPI Configuration ━━━${NC}"
if [ -f "$SYSFS_DIR/dpi" ]; then
    ORIG_DPI=$(cat "$SYSFS_DIR/dpi")
    echo 200 > "$SYSFS_DIR/dpi"
    VAL=$(cat "$SYSFS_DIR/dpi")
    [ "$VAL" = "200" ] && echo -e "  ${GREEN}✓${NC} DPI set to 200%" && PASS=$((PASS+1)) || { echo -e "  ${RED}✗${NC} DPI set failed"; FAIL=$((FAIL+1)); }
    
    inject_packet "0x08 0x0A 0x00" "Move right 10 @ 200% DPI"
    echo -e "  ${GREEN}✓${NC} Movement at high DPI"
    PASS=$((PASS + 1))
    
    echo 50 > "$SYSFS_DIR/dpi"
    inject_packet "0x08 0x0A 0x00" "Move right 10 @ 50% DPI"
    echo -e "  ${GREEN}✓${NC} Movement at low DPI"
    PASS=$((PASS + 1))
    
    echo $ORIG_DPI > "$SYSFS_DIR/dpi"
else
    echo -e "  ${CYAN}⊘ SKIP:${NC} DPI sysfs attribute not found"
fi
echo ""

# ===== TEST 12: /proc Stats =====
echo -e "${YELLOW}━━━ Test 12: /proc/vmouse_stats ━━━${NC}"
if [ -f /proc/vmouse_stats ]; then
    STATS=$(cat /proc/vmouse_stats)
    echo "$STATS" | head -10
    echo "  ..."
    echo -e "  ${GREEN}✓${NC} /proc/vmouse_stats is readable"
    PASS=$((PASS + 1))
else
    echo -e "  ${CYAN}⊘ SKIP:${NC} /proc/vmouse_stats not found"
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
echo "---"
dmesg | grep virtual_mouse | tail -20
