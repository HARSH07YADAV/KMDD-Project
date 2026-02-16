/*
 * reader.c - User-space Input Event Reader (Enhanced v2.0)
 * 
 * Reads events from Linux input devices (/dev/input/eventX)
 * and displays them in human-readable format.
 *
 * Supports keyboard, mouse, and scroll wheel events.
 * Extended key name coverage: F1-F12, arrows, navigation, numpad, multimedia.
 *
 * Usage: ./reader /dev/input/eventX
 *        ./reader /dev/input/eventX --json    (JSON output mode)
 *
 * License: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

/* Color codes for pretty output */
#define COLOR_RESET   "\033[0m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_DIM     "\033[2m"
#define COLOR_BOLD    "\033[1m"

static volatile int running = 1;
static int json_mode = 0;
static unsigned long event_count = 0;

void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
}

/*
 * Convert Linux keycode to readable string
 */
const char* keycode_to_string(unsigned int code)
{
    static char buffer[32];
    
    switch (code) {
        /* Row 1: Escape and Function Keys */
        case KEY_ESC: return "ESC";
        case KEY_F1: return "F1";
        case KEY_F2: return "F2";
        case KEY_F3: return "F3";
        case KEY_F4: return "F4";
        case KEY_F5: return "F5";
        case KEY_F6: return "F6";
        case KEY_F7: return "F7";
        case KEY_F8: return "F8";
        case KEY_F9: return "F9";
        case KEY_F10: return "F10";
        case KEY_F11: return "F11";
        case KEY_F12: return "F12";

        /* Row 2: Numbers */
        case KEY_GRAVE: return "GRAVE";
        case KEY_1: return "1";
        case KEY_2: return "2";
        case KEY_3: return "3";
        case KEY_4: return "4";
        case KEY_5: return "5";
        case KEY_6: return "6";
        case KEY_7: return "7";
        case KEY_8: return "8";
        case KEY_9: return "9";
        case KEY_0: return "0";
        case KEY_MINUS: return "MINUS";
        case KEY_EQUAL: return "EQUAL";
        case KEY_BACKSPACE: return "BACKSPACE";

        /* Row 3: QWERTY */
        case KEY_TAB: return "TAB";
        case KEY_Q: return "Q";
        case KEY_W: return "W";
        case KEY_E: return "E";
        case KEY_R: return "R";
        case KEY_T: return "T";
        case KEY_Y: return "Y";
        case KEY_U: return "U";
        case KEY_I: return "I";
        case KEY_O: return "O";
        case KEY_P: return "P";
        case KEY_LEFTBRACE: return "[";
        case KEY_RIGHTBRACE: return "]";
        case KEY_BACKSLASH: return "\\";

        /* Row 4: Home row */
        case KEY_CAPSLOCK: return "CAPS_LOCK";
        case KEY_A: return "A";
        case KEY_S: return "S";
        case KEY_D: return "D";
        case KEY_F: return "F";
        case KEY_G: return "G";
        case KEY_H: return "H";
        case KEY_J: return "J";
        case KEY_K: return "K";
        case KEY_L: return "L";
        case KEY_SEMICOLON: return ";";
        case KEY_APOSTROPHE: return "'";
        case KEY_ENTER: return "ENTER";

        /* Row 5: Bottom row */
        case KEY_LEFTSHIFT: return "L_SHIFT";
        case KEY_Z: return "Z";
        case KEY_X: return "X";
        case KEY_C: return "C";
        case KEY_V: return "V";
        case KEY_B: return "B";
        case KEY_N: return "N";
        case KEY_M: return "M";
        case KEY_COMMA: return ",";
        case KEY_DOT: return ".";
        case KEY_SLASH: return "/";
        case KEY_RIGHTSHIFT: return "R_SHIFT";

        /* Row 6: Bottom */
        case KEY_LEFTCTRL: return "L_CTRL";
        case KEY_LEFTMETA: return "L_META";
        case KEY_LEFTALT: return "L_ALT";
        case KEY_SPACE: return "SPACE";
        case KEY_RIGHTALT: return "R_ALT";
        case KEY_RIGHTMETA: return "R_META";
        case KEY_COMPOSE: return "MENU";
        case KEY_RIGHTCTRL: return "R_CTRL";

        /* Navigation cluster */
        case KEY_INSERT: return "INSERT";
        case KEY_DELETE: return "DELETE";
        case KEY_HOME: return "HOME";
        case KEY_END: return "END";
        case KEY_PAGEUP: return "PAGE_UP";
        case KEY_PAGEDOWN: return "PAGE_DN";

        /* Arrow keys */
        case KEY_UP: return "↑ UP";
        case KEY_DOWN: return "↓ DOWN";
        case KEY_LEFT: return "← LEFT";
        case KEY_RIGHT: return "→ RIGHT";

        /* Numpad */
        case KEY_NUMLOCK: return "NUM_LOCK";
        case KEY_KP0: return "KP_0";
        case KEY_KP1: return "KP_1";
        case KEY_KP2: return "KP_2";
        case KEY_KP3: return "KP_3";
        case KEY_KP4: return "KP_4";
        case KEY_KP5: return "KP_5";
        case KEY_KP6: return "KP_6";
        case KEY_KP7: return "KP_7";
        case KEY_KP8: return "KP_8";
        case KEY_KP9: return "KP_9";
        case KEY_KPDOT: return "KP_.";
        case KEY_KPPLUS: return "KP_+";
        case KEY_KPMINUS: return "KP_-";
        case KEY_KPASTERISK: return "KP_*";
        case KEY_KPENTER: return "KP_ENTER";
        case KEY_SCROLLLOCK: return "SCROLL_LK";

        /* Multimedia keys */
        case KEY_MUTE: return "♪ MUTE";
        case KEY_VOLUMEDOWN: return "♪ VOL-";
        case KEY_VOLUMEUP: return "♪ VOL+";
        case KEY_PLAYPAUSE: return "♪ PLAY/PAUSE";
        case KEY_STOPCD: return "♪ STOP";
        case KEY_PREVIOUSSONG: return "♪ PREV";
        case KEY_NEXTSONG: return "♪ NEXT";
        case KEY_HOMEPAGE: return "⌂ HOME_PAGE";
        case KEY_MAIL: return "✉ MAIL";
        case KEY_CALC: return "🖩 CALC";
        case KEY_COMPUTER: return "💻 COMPUTER";
        case KEY_SEARCH: return "🔍 SEARCH";
        case KEY_BOOKMARKS: return "🔖 BOOKMARKS";

        /* System keys */
        case KEY_POWER: return "⏻ POWER";
        case KEY_SLEEP: return "💤 SLEEP";
        case KEY_WAKEUP: return "☀ WAKEUP";
        case KEY_102ND: return "102ND";

        /* Mouse buttons */
        case BTN_LEFT: return "MOUSE_LEFT";
        case BTN_RIGHT: return "MOUSE_RIGHT";
        case BTN_MIDDLE: return "MOUSE_MIDDLE";
        case BTN_SIDE: return "MOUSE_SIDE";
        case BTN_EXTRA: return "MOUSE_FORWARD";

        default:
            snprintf(buffer, sizeof(buffer), "KEY_%u", code);
            return buffer;
    }
}

/*
 * Get current timestamp string with milliseconds
 */
void get_timestamp(char *buf, size_t len)
{
    struct timespec ts;
    struct tm *tm_info;
    char time_str[32];

    clock_gettime(CLOCK_REALTIME, &ts);
    tm_info = localtime(&ts.tv_sec);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
    snprintf(buf, len, "%s.%03ld", time_str, ts.tv_nsec / 1000000);
}

/*
 * Print event in JSON format
 */
void print_event_json(struct input_event *ev)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    printf("{\"time\":\"%s\",\"type\":%u,\"code\":%u,\"value\":%d",
           timestamp, ev->type, ev->code, ev->value);

    if (ev->type == EV_KEY) {
        printf(",\"key\":\"%s\",\"action\":\"%s\"",
               keycode_to_string(ev->code),
               ev->value ? "press" : "release");
    } else if (ev->type == EV_REL) {
        const char *axis = "unknown";
        if (ev->code == REL_X) axis = "X";
        else if (ev->code == REL_Y) axis = "Y";
        else if (ev->code == REL_WHEEL) axis = "WHEEL";
        else if (ev->code == REL_HWHEEL) axis = "HWHEEL";
        printf(",\"axis\":\"%s\"", axis);
    }

    printf("}\n");
    fflush(stdout);
}

/*
 * Print event in human-readable format
 */
void print_event(struct input_event *ev)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    
    event_count++;

    switch (ev->type) {
        case EV_KEY:
            if (ev->code >= BTN_MOUSE && ev->code < BTN_JOYSTICK) {
                /* Mouse button */
                printf("%s[%s]%s %s#%-6lu%s %sMOUSE_BTN%s %-18s %s%s%s\n",
                       COLOR_CYAN, timestamp, COLOR_RESET,
                       COLOR_DIM, event_count, COLOR_RESET,
                       COLOR_YELLOW, COLOR_RESET,
                       keycode_to_string(ev->code),
                       ev->value ? COLOR_GREEN : COLOR_RED,
                       ev->value ? "▼ PRESSED" : "▲ RELEASED",
                       COLOR_RESET);
            } else {
                /* Keyboard key */
                const char *action;
                const char *color;
                if (ev->value == 2) {
                    action = "↻ REPEAT";
                    color = COLOR_MAGENTA;
                } else if (ev->value == 1) {
                    action = "▼ PRESSED";
                    color = COLOR_GREEN;
                } else {
                    action = "▲ RELEASED";
                    color = COLOR_RED;
                }

                printf("%s[%s]%s %s#%-6lu%s %sKEY%s       %-18s %s%s%s\n",
                       COLOR_CYAN, timestamp, COLOR_RESET,
                       COLOR_DIM, event_count, COLOR_RESET,
                       COLOR_BLUE, COLOR_RESET,
                       keycode_to_string(ev->code),
                       color, action, COLOR_RESET);
            }
            break;
            
        case EV_REL:
            if (ev->code == REL_X) {
                printf("%s[%s]%s %s#%-6lu%s %sMOUSE%s     X: %s%+4d%s\n",
                       COLOR_CYAN, timestamp, COLOR_RESET,
                       COLOR_DIM, event_count, COLOR_RESET,
                       COLOR_YELLOW, COLOR_RESET,
                       COLOR_BOLD, ev->value, COLOR_RESET);
            } else if (ev->code == REL_Y) {
                printf("%s[%s]%s %s#%-6lu%s %sMOUSE%s     Y: %s%+4d%s\n",
                       COLOR_CYAN, timestamp, COLOR_RESET,
                       COLOR_DIM, event_count, COLOR_RESET,
                       COLOR_YELLOW, COLOR_RESET,
                       COLOR_BOLD, ev->value, COLOR_RESET);
            } else if (ev->code == REL_WHEEL) {
                printf("%s[%s]%s %s#%-6lu%s %sSCROLL%s    ⟳ Wheel: %s%+4d%s\n",
                       COLOR_CYAN, timestamp, COLOR_RESET,
                       COLOR_DIM, event_count, COLOR_RESET,
                       COLOR_MAGENTA, COLOR_RESET,
                       COLOR_BOLD, ev->value, COLOR_RESET);
            } else if (ev->code == REL_HWHEEL) {
                printf("%s[%s]%s %s#%-6lu%s %sSCROLL%s    ⟳ HWheel: %s%+4d%s\n",
                       COLOR_CYAN, timestamp, COLOR_RESET,
                       COLOR_DIM, event_count, COLOR_RESET,
                       COLOR_MAGENTA, COLOR_RESET,
                       COLOR_BOLD, ev->value, COLOR_RESET);
            } else {
                printf("%s[%s]%s %s#%-6lu%s %sREL%s       code=%u value=%d\n",
                       COLOR_CYAN, timestamp, COLOR_RESET,
                       COLOR_DIM, event_count, COLOR_RESET,
                       COLOR_YELLOW, COLOR_RESET,
                       ev->code, ev->value);
            }
            break;
            
        case EV_ABS:
            printf("%s[%s]%s %s#%-6lu%s %sABS%s       code=%u value=%d\n",
                   COLOR_CYAN, timestamp, COLOR_RESET,
                   COLOR_DIM, event_count, COLOR_RESET,
                   COLOR_YELLOW, COLOR_RESET,
                   ev->code, ev->value);
            break;
            
        case EV_SYN:
            if (ev->code == SYN_REPORT) {
                printf("%s[%s]%s %s────── sync ──────%s\n",
                       COLOR_CYAN, timestamp, COLOR_RESET,
                       COLOR_DIM, COLOR_RESET);
            }
            break;

        case EV_LED:
            printf("%s[%s]%s %s#%-6lu%s %sLED%s       %s = %s\n",
                   COLOR_CYAN, timestamp, COLOR_RESET,
                   COLOR_DIM, event_count, COLOR_RESET,
                   COLOR_GREEN, COLOR_RESET,
                   ev->code == LED_CAPSL ? "CAPS_LOCK" :
                   ev->code == LED_NUML ? "NUM_LOCK" :
                   ev->code == LED_SCROLLL ? "SCROLL_LOCK" : "UNKNOWN",
                   ev->value ? "ON" : "OFF");
            break;

        case EV_REP:
            printf("%s[%s]%s %s#%-6lu%s %sREPEAT%s    %s = %d\n",
                   COLOR_CYAN, timestamp, COLOR_RESET,
                   COLOR_DIM, event_count, COLOR_RESET,
                   COLOR_MAGENTA, COLOR_RESET,
                   ev->code == REP_DELAY ? "delay_ms" : "period_ms",
                   ev->value);
            break;

        case EV_MSC:
            printf("%s[%s]%s %s#%-6lu%s %sMSC%s       code=%u value=%d\n",
                   COLOR_CYAN, timestamp, COLOR_RESET,
                   COLOR_DIM, event_count, COLOR_RESET,
                   COLOR_YELLOW, COLOR_RESET,
                   ev->code, ev->value);
            break;
            
        default:
            printf("%s[%s]%s %s#%-6lu%s %sUNKNOWN%s   type=%u code=%u value=%d\n",
                   COLOR_CYAN, timestamp, COLOR_RESET,
                   COLOR_DIM, event_count, COLOR_RESET,
                   COLOR_YELLOW, COLOR_RESET,
                   ev->type, ev->code, ev->value);
            break;
    }
    
    fflush(stdout);
}

/*
 * Get device name
 */
int get_device_name(int fd, char *name, size_t len)
{
    if (ioctl(fd, EVIOCGNAME(len), name) < 0)
        return -1;
    return 0;
}

/*
 * Get device info
 */
void print_device_info(int fd, const char *path)
{
    char device_name[256] = "Unknown Device";
    struct input_id id;
    unsigned long evbits[EV_MAX / sizeof(unsigned long) + 1];

    get_device_name(fd, device_name, sizeof(device_name));
    ioctl(fd, EVIOCGID, &id);

    printf("\n");
    printf("%s╔══════════════════════════════════════════╗%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s║  Input Event Reader v2.0                 ║%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s╠══════════════════════════════════════════╣%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s║%s Device:  %-32s%s║%s\n", COLOR_CYAN, COLOR_RESET, path, COLOR_CYAN, COLOR_RESET);
    printf("%s║%s Name:    %-32s%s║%s\n", COLOR_CYAN, COLOR_RESET, device_name, COLOR_CYAN, COLOR_RESET);
    printf("%s║%s Bus:     0x%04x  Vendor: 0x%04x          %s║%s\n",
           COLOR_CYAN, COLOR_RESET, id.bustype, id.vendor, COLOR_CYAN, COLOR_RESET);
    printf("%s║%s Product: 0x%04x  Version: 0x%04x         %s║%s\n",
           COLOR_CYAN, COLOR_RESET, id.product, id.version, COLOR_CYAN, COLOR_RESET);

    /* Show supported event types */
    memset(evbits, 0, sizeof(evbits));
    ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
    printf("%s║%s Events:  ", COLOR_CYAN, COLOR_RESET);
    if (evbits[0] & (1 << EV_KEY)) printf("KEY ");
    if (evbits[0] & (1 << EV_REL)) printf("REL ");
    if (evbits[0] & (1 << EV_ABS)) printf("ABS ");
    if (evbits[0] & (1 << EV_REP)) printf("REP ");
    if (evbits[0] & (1 << EV_LED)) printf("LED ");
    if (evbits[0] & (1 << EV_MSC)) printf("MSC ");
    printf("%-20s%s║%s\n", "", COLOR_CYAN, COLOR_RESET);

    if (json_mode)
        printf("%s║%s Output:  %-32s%s║%s\n", COLOR_CYAN, COLOR_RESET, "JSON", COLOR_CYAN, COLOR_RESET);

    printf("%s╠══════════════════════════════════════════╣%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s║%s  Listening... Press Ctrl+C to exit       %s║%s\n", COLOR_CYAN, COLOR_RESET, COLOR_CYAN, COLOR_RESET);
    printf("%s╚══════════════════════════════════════════╝%s\n\n", COLOR_CYAN, COLOR_RESET);
}

/*
 * Main function
 */
int main(int argc, char *argv[])
{
    int fd;
    struct input_event ev;
    ssize_t bytes;
    
    /* Check arguments */
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s /dev/input/eventX [--json]\n\n", argv[0]);
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  --json    Output events in JSON format\n\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s /dev/input/event0\n", argv[0]);
        fprintf(stderr, "  %s /dev/input/event0 --json\n", argv[0]);
        fprintf(stderr, "\nTip: Use 'cat /proc/bus/input/devices' to find devices\n");
        return 1;
    }

    if (argc == 3 && strcmp(argv[2], "--json") == 0)
        json_mode = 1;

    /* Setup signal handler for clean exit */
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    
    /* Open input device */
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", argv[1], strerror(errno));
        fprintf(stderr, "Try running with sudo: sudo %s %s\n", argv[0], argv[1]);
        return 1;
    }
    
    /* Print device info */
    print_device_info(fd, argv[1]);

    /* Read events in a loop */
    while (running) {
        bytes = read(fd, &ev, sizeof(ev));
        
        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "\nError reading event: %s\n", strerror(errno));
            break;
        }
        
        if (bytes != sizeof(ev)) {
            fprintf(stderr, "\nError: Short read (got %zd bytes, expected %zu)\n",
                    bytes, sizeof(ev));
            break;
        }
        
        /* Print the event */
        if (json_mode)
            print_event_json(&ev);
        else
            print_event(&ev);
    }

    printf("\n%s--- Reader stopped. Total events: %lu ---%s\n",
           COLOR_DIM, event_count, COLOR_RESET);
    
    close(fd);
    return 0;
}
