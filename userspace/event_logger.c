/*
 * event_logger.c - Input Event Logger Daemon
 *
 * Background daemon that reads /dev/input/eventX and writes structured
 * JSON logs to a log file. Supports:
 * - JSON structured logging
 * - Log file rotation (by size)
 * - Device type filtering (keyboard/mouse/touchpad/all)
 * - Configurable max log file size
 * - Graceful shutdown on SIGINT/SIGTERM
 *
 * Builds with or without libjson-c (falls back to manual JSON formatting).
 *
 * Usage:
 *   ./event_logger /dev/input/eventX                      # Log to stdout
 *   ./event_logger /dev/input/eventX -o logs/events.json  # Log to file
 *   ./event_logger /dev/input/eventX -o logs/events.json -m 5  # Max 5MB, rotate
 *   ./event_logger /dev/input/eventX -f keyboard           # Filter keyboard only
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
#include <getopt.h>

#define DEFAULT_MAX_SIZE_MB  10
#define MAX_ROTATIONS        5
#define VERSION              "1.0"

static volatile int running = 1;
static unsigned long event_count = 0;

/* Configuration */
static struct {
    const char *device_path;
    const char *output_path;   /* NULL = stdout */
    int max_size_mb;
    const char *filter;        /* "all", "keyboard", "mouse", "touchpad" */
    int daemon_mode;
} config = {
    .device_path = NULL,
    .output_path = NULL,
    .max_size_mb = DEFAULT_MAX_SIZE_MB,
    .filter = "all",
    .daemon_mode = 0,
};

void sighandler(int sig)
{
    (void)sig;
    running = 0;
}

/*
 * Get event type name
 */
static const char *event_type_name(unsigned short type)
{
    switch (type) {
        case EV_SYN: return "SYN";
        case EV_KEY: return "KEY";
        case EV_REL: return "REL";
        case EV_ABS: return "ABS";
        case EV_MSC: return "MSC";
        case EV_LED: return "LED";
        case EV_REP: return "REP";
        default: return "UNKNOWN";
    }
}

/*
 * Get key/button name
 */
static const char *key_name(unsigned int code)
{
    static char buf[32];

    /* Common keyboard keys */
    switch (code) {
        case KEY_ESC: return "ESC";
        case KEY_1: return "1"; case KEY_2: return "2"; case KEY_3: return "3";
        case KEY_4: return "4"; case KEY_5: return "5"; case KEY_6: return "6";
        case KEY_7: return "7"; case KEY_8: return "8"; case KEY_9: return "9";
        case KEY_0: return "0";
        case KEY_BACKSPACE: return "BACKSPACE"; case KEY_TAB: return "TAB";
        case KEY_Q: return "Q"; case KEY_W: return "W"; case KEY_E: return "E";
        case KEY_R: return "R"; case KEY_T: return "T"; case KEY_Y: return "Y";
        case KEY_U: return "U"; case KEY_I: return "I"; case KEY_O: return "O";
        case KEY_P: return "P"; case KEY_ENTER: return "ENTER";
        case KEY_A: return "A"; case KEY_S: return "S"; case KEY_D: return "D";
        case KEY_F: return "F"; case KEY_G: return "G"; case KEY_H: return "H";
        case KEY_J: return "J"; case KEY_K: return "K"; case KEY_L: return "L";
        case KEY_Z: return "Z"; case KEY_X: return "X"; case KEY_C: return "C";
        case KEY_V: return "V"; case KEY_B: return "B"; case KEY_N: return "N";
        case KEY_M: return "M"; case KEY_SPACE: return "SPACE";
        case KEY_LEFTSHIFT: return "L_SHIFT"; case KEY_RIGHTSHIFT: return "R_SHIFT";
        case KEY_LEFTCTRL: return "L_CTRL"; case KEY_RIGHTCTRL: return "R_CTRL";
        case KEY_LEFTALT: return "L_ALT"; case KEY_RIGHTALT: return "R_ALT";
        case KEY_UP: return "UP"; case KEY_DOWN: return "DOWN";
        case KEY_LEFT: return "LEFT"; case KEY_RIGHT: return "RIGHT";
        case KEY_F1: return "F1"; case KEY_F2: return "F2"; case KEY_F3: return "F3";
        case KEY_F4: return "F4"; case KEY_F5: return "F5"; case KEY_F6: return "F6";
        case KEY_F7: return "F7"; case KEY_F8: return "F8"; case KEY_F9: return "F9";
        case KEY_F10: return "F10"; case KEY_F11: return "F11"; case KEY_F12: return "F12";
        case KEY_DELETE: return "DELETE"; case KEY_INSERT: return "INSERT";
        case KEY_HOME: return "HOME"; case KEY_END: return "END";
        case KEY_PAGEUP: return "PAGEUP"; case KEY_PAGEDOWN: return "PAGEDOWN";
        case KEY_CAPSLOCK: return "CAPSLOCK"; case KEY_NUMLOCK: return "NUMLOCK";

        /* Mouse buttons */
        case BTN_LEFT: return "BTN_LEFT"; case BTN_RIGHT: return "BTN_RIGHT";
        case BTN_MIDDLE: return "BTN_MIDDLE";
        case BTN_SIDE: return "BTN_SIDE"; case BTN_EXTRA: return "BTN_EXTRA";
        case BTN_TOUCH: return "BTN_TOUCH";

        default:
            snprintf(buf, sizeof(buf), "KEY_%u", code);
            return buf;
    }
}

/*
 * Get relative axis name
 */
static const char *rel_name(unsigned int code)
{
    switch (code) {
        case REL_X: return "REL_X";
        case REL_Y: return "REL_Y";
        case REL_WHEEL: return "REL_WHEEL";
        case REL_HWHEEL: return "REL_HWHEEL";
        default: return "REL_UNKNOWN";
    }
}

/*
 * Get absolute axis name
 */
static const char *abs_name(unsigned int code)
{
    switch (code) {
        case ABS_X: return "ABS_X";
        case ABS_Y: return "ABS_Y";
        case ABS_PRESSURE: return "ABS_PRESSURE";
        case ABS_MT_SLOT: return "ABS_MT_SLOT";
        case ABS_MT_POSITION_X: return "ABS_MT_X";
        case ABS_MT_POSITION_Y: return "ABS_MT_Y";
        case ABS_MT_PRESSURE: return "ABS_MT_PRESSURE";
        case ABS_MT_TRACKING_ID: return "ABS_MT_TRACKING_ID";
        default: return "ABS_UNKNOWN";
    }
}

/*
 * Check if event matches filter
 */
static int should_log(struct input_event *ev)
{
    if (strcmp(config.filter, "all") == 0)
        return 1;

    if (strcmp(config.filter, "keyboard") == 0) {
        if (ev->type == EV_KEY && ev->code < BTN_MOUSE)
            return 1;
        if (ev->type == EV_SYN || ev->type == EV_REP || ev->type == EV_LED)
            return 1;
        return 0;
    }

    if (strcmp(config.filter, "mouse") == 0) {
        if (ev->type == EV_REL)
            return 1;
        if (ev->type == EV_KEY && ev->code >= BTN_MOUSE && ev->code < BTN_JOYSTICK)
            return 1;
        if (ev->type == EV_SYN)
            return 1;
        return 0;
    }

    if (strcmp(config.filter, "touchpad") == 0) {
        if (ev->type == EV_ABS)
            return 1;
        if (ev->type == EV_KEY && (ev->code == BTN_TOUCH ||
            ev->code == BTN_LEFT || ev->code == BTN_RIGHT))
            return 1;
        if (ev->type == EV_SYN)
            return 1;
        return 0;
    }

    return 1;
}

/*
 * Get ISO 8601 timestamp with milliseconds
 */
static void iso_timestamp(char *buf, size_t len)
{
    struct timespec ts;
    struct tm *tm_info;
    char time_str[64];

    clock_gettime(CLOCK_REALTIME, &ts);
    tm_info = localtime(&ts.tv_sec);
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", tm_info);
    snprintf(buf, len, "%s.%03ldZ", time_str, ts.tv_nsec / 1000000);
}

/*
 * Write JSON log entry (manual formatting, no libjson-c needed)
 */
static void write_json_event(FILE *fp, struct input_event *ev)
{
    char ts[128];
    iso_timestamp(ts, sizeof(ts));

    event_count++;

    fprintf(fp, "{\"id\":%lu,\"time\":\"%s\",\"type\":\"%s\",\"type_id\":%u,\"code\":%u,\"value\":%d",
            event_count, ts, event_type_name(ev->type), ev->type, ev->code, ev->value);

    /* Add decoded field based on type */
    switch (ev->type) {
        case EV_KEY:
            fprintf(fp, ",\"key\":\"%s\",\"action\":\"%s\"",
                    key_name(ev->code),
                    ev->value == 2 ? "repeat" : (ev->value ? "press" : "release"));
            break;
        case EV_REL:
            fprintf(fp, ",\"axis\":\"%s\"", rel_name(ev->code));
            break;
        case EV_ABS:
            fprintf(fp, ",\"axis\":\"%s\"", abs_name(ev->code));
            break;
        default:
            break;
    }

    fprintf(fp, "}\n");
    fflush(fp);
}

/*
 * Rotate log files
 * events.json -> events.json.1 -> events.json.2 -> ...
 */
static void rotate_log(const char *path)
{
    char old_name[512], new_name[512];
    int i;

    /* Remove oldest rotation */
    snprintf(old_name, sizeof(old_name), "%s.%d", path, MAX_ROTATIONS);
    unlink(old_name);

    /* Shift existing rotations */
    for (i = MAX_ROTATIONS - 1; i >= 1; i--) {
        snprintf(old_name, sizeof(old_name), "%s.%d", path, i);
        snprintf(new_name, sizeof(new_name), "%s.%d", path, i + 1);
        rename(old_name, new_name);
    }

    /* Rotate current */
    snprintf(new_name, sizeof(new_name), "%s.1", path);
    rename(path, new_name);

    fprintf(stderr, "[logger] Rotated log file -> %s\n", new_name);
}

/*
 * Check log file size and rotate if needed
 */
static FILE *check_rotation(FILE *fp)
{
    struct stat st;
    long max_bytes;

    if (!config.output_path || !fp)
        return fp;

    max_bytes = (long)config.max_size_mb * 1024 * 1024;

    if (fstat(fileno(fp), &st) == 0 && st.st_size >= max_bytes) {
        fclose(fp);
        rotate_log(config.output_path);
        fp = fopen(config.output_path, "a");
        if (!fp) {
            fprintf(stderr, "[logger] Error: Cannot reopen log file after rotation\n");
            running = 0;
            return NULL;
        }
    }

    return fp;
}

/*
 * Print usage
 */
static void usage(const char *prog)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Event Logger Daemon v%s\n", VERSION);
    fprintf(stderr, "Reads input events and writes structured JSON logs.\n\n");
    fprintf(stderr, "Usage: %s <device> [options]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <path>   Output log file (default: stdout)\n");
    fprintf(stderr, "  -m <MB>     Max log file size in MB before rotation (default: %d)\n",
            DEFAULT_MAX_SIZE_MB);
    fprintf(stderr, "  -f <type>   Filter: all, keyboard, mouse, touchpad (default: all)\n");
    fprintf(stderr, "  -d          Run as background daemon\n");
    fprintf(stderr, "  -h          Show this help\n\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s /dev/input/event0\n", prog);
    fprintf(stderr, "  %s /dev/input/event0 -o logs/events.json\n", prog);
    fprintf(stderr, "  %s /dev/input/event0 -o logs/events.json -m 5 -f keyboard\n", prog);
    fprintf(stderr, "\n");
}

int main(int argc, char *argv[])
{
    int fd, opt;
    struct input_event ev;
    ssize_t bytes;
    FILE *output = stdout;
    char device_name[256] = "Unknown";
    long rotation_check_counter = 0;

    while ((opt = getopt(argc, argv, "o:m:f:dh")) != -1) {
        switch (opt) {
            case 'o': config.output_path = optarg; break;
            case 'm': config.max_size_mb = atoi(optarg); break;
            case 'f': config.filter = optarg; break;
            case 'd': config.daemon_mode = 1; break;
            case 'h': usage(argv[0]); return 0;
            default: usage(argv[0]); return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: No device path specified.\n");
        usage(argv[0]);
        return 1;
    }

    config.device_path = argv[optind];

    /* Validate filter */
    if (strcmp(config.filter, "all") != 0 &&
        strcmp(config.filter, "keyboard") != 0 &&
        strcmp(config.filter, "mouse") != 0 &&
        strcmp(config.filter, "touchpad") != 0) {
        fprintf(stderr, "Error: Invalid filter '%s'. Use: all, keyboard, mouse, touchpad\n",
                config.filter);
        return 1;
    }

    /* Setup signal handlers */
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    /* Daemonize if requested */
    if (config.daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid > 0) {
            printf("[logger] Daemon started with PID %d\n", pid);
            return 0;
        }
        setsid();
    }

    /* Open input device */
    fd = open(config.device_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open %s: %s\n",
                config.device_path, strerror(errno));
        return 1;
    }

    ioctl(fd, EVIOCGNAME(sizeof(device_name)), device_name);

    /* Open output file */
    if (config.output_path) {
        output = fopen(config.output_path, "a");
        if (!output) {
            fprintf(stderr, "Error: Cannot open %s: %s\n",
                    config.output_path, strerror(errno));
            close(fd);
            return 1;
        }
    }

    /* Write header */
    if (!config.daemon_mode) {
        fprintf(stderr, "╔═══════════════════════════════════════╗\n");
        fprintf(stderr, "║  Event Logger v%s                    ║\n", VERSION);
        fprintf(stderr, "╠═══════════════════════════════════════╣\n");
        fprintf(stderr, "║  Device:  %-27s ║\n", config.device_path);
        fprintf(stderr, "║  Name:    %-27s ║\n", device_name);
        fprintf(stderr, "║  Output:  %-27s ║\n",
                config.output_path ? config.output_path : "stdout");
        fprintf(stderr, "║  Filter:  %-27s ║\n", config.filter);
        fprintf(stderr, "║  Max Log: %-2d MB                       ║\n", config.max_size_mb);
        fprintf(stderr, "╠═══════════════════════════════════════╣\n");
        fprintf(stderr, "║  Logging... Press Ctrl+C to stop      ║\n");
        fprintf(stderr, "╚═══════════════════════════════════════╝\n\n");
    }

    /* Main event loop */
    while (running) {
        bytes = read(fd, &ev, sizeof(ev));

        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "[logger] Read error: %s\n", strerror(errno));
            break;
        }

        if (bytes != sizeof(ev))
            continue;

        /* Apply filter */
        if (!should_log(&ev))
            continue;

        /* Skip SYN events in log (too noisy) */
        if (ev.type == EV_SYN)
            continue;

        /* Write JSON */
        write_json_event(output, &ev);

        /* Check rotation every 100 events */
        if (config.output_path && ++rotation_check_counter >= 100) {
            output = check_rotation(output);
            rotation_check_counter = 0;
            if (!output)
                break;
        }
    }

    /* Cleanup */
    if (!config.daemon_mode)
        fprintf(stderr, "\n[logger] Stopped. Total events logged: %lu\n", event_count);

    if (output != stdout)
        fclose(output);
    close(fd);

    return 0;
}
