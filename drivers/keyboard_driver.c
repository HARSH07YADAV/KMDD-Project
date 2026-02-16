/*
 * keyboard_driver.c - Virtual PS/2 Keyboard Driver (Enhanced)
 * 
 * Educational Linux kernel module demonstrating:
 * - Input subsystem integration
 * - Extended scan code to keycode translation (full US layout + multimedia)
 * - IRQ simulation with tasklets
 * - Sysfs interface for testing & LED control
 * - /proc statistics interface
 * - Module parameters for repeat rate configuration
 * - Combo/macro key detection
 * - Proper locking and buffering
 *
 * License: MIT
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/jiffies.h>

#define DRIVER_NAME "virtual_keyboard"
#define BUFFER_SIZE 256

/* Module parameters for key repeat configuration */
static int repeat_delay = 250;   /* ms before repeat starts */
static int repeat_rate = 33;     /* ms between repeats */
module_param(repeat_delay, int, 0644);
MODULE_PARM_DESC(repeat_delay, "Key repeat delay in ms (default: 250)");
module_param(repeat_rate, int, 0644);
MODULE_PARM_DESC(repeat_rate, "Key repeat interval in ms (default: 33)");

/* Driver data structure */
struct vkbd_device {
    struct input_dev *input;
    struct tasklet_struct tasklet;
    spinlock_t buffer_lock;
    unsigned char buffer[BUFFER_SIZE];
    unsigned int head;
    unsigned int tail;
    bool shift_pressed;
    bool ctrl_pressed;
    bool alt_pressed;

    /* LED state */
    bool caps_lock;
    bool num_lock;
    bool scroll_lock;

    /* Statistics */
    unsigned long total_keypresses;
    unsigned long total_keyreleases;
    unsigned long buffer_overflows;
    unsigned long unknown_scancodes;
    unsigned long combo_detections;
    unsigned long start_jiffies;
};

static struct vkbd_device *vkbd_dev;
static struct proc_dir_entry *vkbd_proc_entry;

/*
 * Extended Scan Code to Linux Keycode Translation Table
 * PS/2 Set 1 scan codes (make codes, release = make | 0x80)
 * Full US keyboard layout + function keys F1-F12 + navigation + numpad
 */
static const unsigned short scancode_to_keycode[128] = {
    /* 0x00 */ 0,
    /* 0x01 */ KEY_ESC,
    /* 0x02 */ KEY_1,
    /* 0x03 */ KEY_2,
    /* 0x04 */ KEY_3,
    /* 0x05 */ KEY_4,
    /* 0x06 */ KEY_5,
    /* 0x07 */ KEY_6,
    /* 0x08 */ KEY_7,
    /* 0x09 */ KEY_8,
    /* 0x0A */ KEY_9,
    /* 0x0B */ KEY_0,
    /* 0x0C */ KEY_MINUS,
    /* 0x0D */ KEY_EQUAL,
    /* 0x0E */ KEY_BACKSPACE,
    /* 0x0F */ KEY_TAB,
    /* 0x10 */ KEY_Q,
    /* 0x11 */ KEY_W,
    /* 0x12 */ KEY_E,
    /* 0x13 */ KEY_R,
    /* 0x14 */ KEY_T,
    /* 0x15 */ KEY_Y,
    /* 0x16 */ KEY_U,
    /* 0x17 */ KEY_I,
    /* 0x18 */ KEY_O,
    /* 0x19 */ KEY_P,
    /* 0x1A */ KEY_LEFTBRACE,
    /* 0x1B */ KEY_RIGHTBRACE,
    /* 0x1C */ KEY_ENTER,
    /* 0x1D */ KEY_LEFTCTRL,
    /* 0x1E */ KEY_A,
    /* 0x1F */ KEY_S,
    /* 0x20 */ KEY_D,
    /* 0x21 */ KEY_F,
    /* 0x22 */ KEY_G,
    /* 0x23 */ KEY_H,
    /* 0x24 */ KEY_J,
    /* 0x25 */ KEY_K,
    /* 0x26 */ KEY_L,
    /* 0x27 */ KEY_SEMICOLON,
    /* 0x28 */ KEY_APOSTROPHE,
    /* 0x29 */ KEY_GRAVE,
    /* 0x2A */ KEY_LEFTSHIFT,
    /* 0x2B */ KEY_BACKSLASH,
    /* 0x2C */ KEY_Z,
    /* 0x2D */ KEY_X,
    /* 0x2E */ KEY_C,
    /* 0x2F */ KEY_V,
    /* 0x30 */ KEY_B,
    /* 0x31 */ KEY_N,
    /* 0x32 */ KEY_M,
    /* 0x33 */ KEY_COMMA,
    /* 0x34 */ KEY_DOT,
    /* 0x35 */ KEY_SLASH,
    /* 0x36 */ KEY_RIGHTSHIFT,
    /* 0x37 */ KEY_KPASTERISK,
    /* 0x38 */ KEY_LEFTALT,
    /* 0x39 */ KEY_SPACE,
    /* 0x3A */ KEY_CAPSLOCK,
    /* 0x3B */ KEY_F1,
    /* 0x3C */ KEY_F2,
    /* 0x3D */ KEY_F3,
    /* 0x3E */ KEY_F4,
    /* 0x3F */ KEY_F5,
    /* 0x40 */ KEY_F6,
    /* 0x41 */ KEY_F7,
    /* 0x42 */ KEY_F8,
    /* 0x43 */ KEY_F9,
    /* 0x44 */ KEY_F10,
    /* 0x45 */ KEY_NUMLOCK,
    /* 0x46 */ KEY_SCROLLLOCK,
    /* 0x47 */ KEY_KP7,
    /* 0x48 */ KEY_KP8,
    /* 0x49 */ KEY_KP9,
    /* 0x4A */ KEY_KPMINUS,
    /* 0x4B */ KEY_KP4,
    /* 0x4C */ KEY_KP5,
    /* 0x4D */ KEY_KP6,
    /* 0x4E */ KEY_KPPLUS,
    /* 0x4F */ KEY_KP1,
    /* 0x50 */ KEY_KP2,
    /* 0x51 */ KEY_KP3,
    /* 0x52 */ KEY_KP0,
    /* 0x53 */ KEY_KPDOT,
    /* 0x54 */ 0,
    /* 0x55 */ 0,
    /* 0x56 */ KEY_102ND,
    /* 0x57 */ KEY_F11,
    /* 0x58 */ KEY_F12,
    /* 0x59 */ 0,
    /* 0x5A */ 0,
    /* 0x5B */ KEY_LEFTMETA,   /* Windows/Super key */
    /* 0x5C */ KEY_RIGHTMETA,
    /* 0x5D */ KEY_COMPOSE,    /* Menu key */
    /* 0x5E */ KEY_POWER,
    /* 0x5F */ KEY_SLEEP,
    /* 0x60 */ 0,
    /* 0x61 */ 0,
    /* 0x62 */ 0,
    /* 0x63 */ KEY_WAKEUP,
    /* 0x64 */ 0,
    /* 0x65 */ KEY_SEARCH,
    /* 0x66 */ KEY_BOOKMARKS,
    /* 0x67 */ KEY_UP,          /* Arrow Up */
    /* 0x68 */ KEY_PAGEUP,
    /* 0x69 */ KEY_LEFT,        /* Arrow Left */
    /* 0x6A */ KEY_RIGHT,       /* Arrow Right */
    /* 0x6B */ KEY_END,
    /* 0x6C */ KEY_DOWN,        /* Arrow Down */
    /* 0x6D */ KEY_PAGEDOWN,
    /* 0x6E */ KEY_INSERT,
    /* 0x6F */ KEY_DELETE,
    /* 0x70 */ 0,
    /* 0x71 */ KEY_MUTE,
    /* 0x72 */ KEY_VOLUMEDOWN,
    /* 0x73 */ KEY_VOLUMEUP,
    /* 0x74 */ KEY_PLAYPAUSE,
    /* 0x75 */ KEY_STOPCD,
    /* 0x76 */ KEY_PREVIOUSSONG,
    /* 0x77 */ KEY_NEXTSONG,
    /* 0x78 */ KEY_HOMEPAGE,
    /* 0x79 */ KEY_MAIL,
    /* 0x7A */ KEY_CALC,
    /* 0x7B */ KEY_COMPUTER,
    /* 0x7C */ KEY_KPENTER,
    /* 0x7D */ KEY_RIGHTCTRL,
    /* 0x7E */ KEY_RIGHTALT,
    /* 0x7F */ KEY_HOME,
};

/*
 * Buffer Management Functions
 * Circular buffer for scan codes with proper locking
 */
static bool buffer_empty(struct vkbd_device *dev)
{
    return dev->head == dev->tail;
}

static bool buffer_full(struct vkbd_device *dev)
{
    return ((dev->head + 1) % BUFFER_SIZE) == dev->tail;
}

static void buffer_push(struct vkbd_device *dev, unsigned char scancode)
{
    unsigned long flags;
    
    spin_lock_irqsave(&dev->buffer_lock, flags);
    
    if (!buffer_full(dev)) {
        dev->buffer[dev->head] = scancode;
        dev->head = (dev->head + 1) % BUFFER_SIZE;
    } else {
        dev->buffer_overflows++;
        pr_warn_ratelimited("%s: Buffer overflow (#%lu), dropping scan code 0x%02x\n",
                DRIVER_NAME, dev->buffer_overflows, scancode);
    }
    
    spin_unlock_irqrestore(&dev->buffer_lock, flags);
}

static int buffer_pop(struct vkbd_device *dev, unsigned char *scancode)
{
    unsigned long flags;
    int ret = 0;
    
    spin_lock_irqsave(&dev->buffer_lock, flags);
    
    if (!buffer_empty(dev)) {
        *scancode = dev->buffer[dev->tail];
        dev->tail = (dev->tail + 1) % BUFFER_SIZE;
        ret = 1;
    }
    
    spin_unlock_irqrestore(&dev->buffer_lock, flags);
    
    return ret;
}

/*
 * Combo Detection
 * Checks if any well-known combos are active and logs them
 */
static void check_combos(struct vkbd_device *dev, unsigned short keycode, bool pressed)
{
    if (!pressed)
        return;

    /* Ctrl+C */
    if (dev->ctrl_pressed && keycode == KEY_C) {
        dev->combo_detections++;
        pr_info("%s: COMBO detected: Ctrl+C (SIGINT)\n", DRIVER_NAME);
    }
    /* Ctrl+Z */
    if (dev->ctrl_pressed && keycode == KEY_Z) {
        dev->combo_detections++;
        pr_info("%s: COMBO detected: Ctrl+Z (SIGTSTP)\n", DRIVER_NAME);
    }
    /* Ctrl+V */
    if (dev->ctrl_pressed && keycode == KEY_V) {
        dev->combo_detections++;
        pr_info("%s: COMBO detected: Ctrl+V (Paste)\n", DRIVER_NAME);
    }
    /* Ctrl+X */
    if (dev->ctrl_pressed && keycode == KEY_X) {
        dev->combo_detections++;
        pr_info("%s: COMBO detected: Ctrl+X (Cut)\n", DRIVER_NAME);
    }
    /* Alt+Tab */
    if (dev->alt_pressed && keycode == KEY_TAB) {
        dev->combo_detections++;
        pr_info("%s: COMBO detected: Alt+Tab (Switch Window)\n", DRIVER_NAME);
    }
    /* Alt+F4 */
    if (dev->alt_pressed && keycode == KEY_F4) {
        dev->combo_detections++;
        pr_info("%s: COMBO detected: Alt+F4 (Close Window)\n", DRIVER_NAME);
    }
    /* Ctrl+Alt+Delete */
    if (dev->ctrl_pressed && dev->alt_pressed && keycode == KEY_DELETE) {
        dev->combo_detections++;
        pr_info("%s: COMBO detected: Ctrl+Alt+Delete\n", DRIVER_NAME);
    }
}

/*
 * Tasklet Bottom-Half Handler
 * Processes buffered scan codes and reports events to input subsystem
 */
static void vkbd_tasklet_handler(unsigned long data)
{
    struct vkbd_device *dev = (struct vkbd_device *)data;
    unsigned char scancode;
    unsigned short keycode;
    bool key_release;
    
    while (buffer_pop(dev, &scancode)) {
        /* Check if this is a key release (bit 7 set) */
        key_release = (scancode & 0x80) != 0;
        scancode &= 0x7F;  /* Clear release bit to get base scan code */
        
        /* Translate scan code to Linux keycode */
        if (scancode >= ARRAY_SIZE(scancode_to_keycode)) {
            dev->unknown_scancodes++;
            pr_debug("%s: Unknown scan code 0x%02x\n", DRIVER_NAME, scancode);
            continue;
        }
        
        keycode = scancode_to_keycode[scancode];
        if (keycode == 0) {
            dev->unknown_scancodes++;
            pr_debug("%s: No mapping for scan code 0x%02x\n", DRIVER_NAME, scancode);
            continue;
        }
        
        /* Track modifier key states */
        if (keycode == KEY_LEFTSHIFT || keycode == KEY_RIGHTSHIFT)
            dev->shift_pressed = !key_release;
        if (keycode == KEY_LEFTCTRL || keycode == KEY_RIGHTCTRL)
            dev->ctrl_pressed = !key_release;
        if (keycode == KEY_LEFTALT || keycode == KEY_RIGHTALT)
            dev->alt_pressed = !key_release;

        /* Toggle LED states on press */
        if (!key_release) {
            if (keycode == KEY_CAPSLOCK)
                dev->caps_lock = !dev->caps_lock;
            if (keycode == KEY_NUMLOCK)
                dev->num_lock = !dev->num_lock;
            if (keycode == KEY_SCROLLLOCK)
                dev->scroll_lock = !dev->scroll_lock;
        }

        /* Check for key combos */
        check_combos(dev, keycode, !key_release);

        /* Update statistics */
        if (key_release)
            dev->total_keyreleases++;
        else
            dev->total_keypresses++;

        /* Report key event to input subsystem */
        input_report_key(dev->input, keycode, !key_release);
        input_sync(dev->input);
        
        pr_debug("%s: Scan code 0x%02x -> keycode %d (%s)%s%s%s\n",
                 DRIVER_NAME, scancode, keycode, 
                 key_release ? "release" : "press",
                 dev->shift_pressed ? " [SHIFT]" : "",
                 dev->ctrl_pressed ? " [CTRL]" : "",
                 dev->alt_pressed ? " [ALT]" : "");
    }
}

/*
 * Simulated IRQ Handler (Top Half)
 */
static void vkbd_simulate_irq(unsigned char scancode)
{
    buffer_push(vkbd_dev, scancode);
    tasklet_schedule(&vkbd_dev->tasklet);
}

/*
 * Sysfs Interfaces
 */

/* Inject scan code */
static ssize_t inject_scancode_store(struct device *dev,
                                      struct device_attribute *attr,
                                      const char *buf, size_t count)
{
    unsigned long scancode;
    int ret;
    
    ret = kstrtoul(buf, 0, &scancode);
    if (ret)
        return ret;
    
    if (scancode > 0xFF) {
        pr_warn("%s: Invalid scan code 0x%lx (must be 0-255)\n",
                DRIVER_NAME, scancode);
        return -EINVAL;
    }
    
    pr_info("%s: Injecting scan code 0x%02lx\n", DRIVER_NAME, scancode);
    vkbd_simulate_irq((unsigned char)scancode);
    
    return count;
}
static DEVICE_ATTR_WO(inject_scancode);

/* LED states (read/write) */
static ssize_t led_caps_show(struct device *dev,
                             struct device_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%d\n", vkbd_dev->caps_lock ? 1 : 0);
}
static ssize_t led_caps_store(struct device *dev,
                              struct device_attribute *attr,
                              const char *buf, size_t count)
{
    int val;
    if (kstrtoint(buf, 0, &val))
        return -EINVAL;
    vkbd_dev->caps_lock = !!val;
    pr_info("%s: Caps Lock LED %s\n", DRIVER_NAME, val ? "ON" : "OFF");
    return count;
}
static DEVICE_ATTR_RW(led_caps);

static ssize_t led_num_show(struct device *dev,
                            struct device_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%d\n", vkbd_dev->num_lock ? 1 : 0);
}
static ssize_t led_num_store(struct device *dev,
                             struct device_attribute *attr,
                             const char *buf, size_t count)
{
    int val;
    if (kstrtoint(buf, 0, &val))
        return -EINVAL;
    vkbd_dev->num_lock = !!val;
    pr_info("%s: Num Lock LED %s\n", DRIVER_NAME, val ? "ON" : "OFF");
    return count;
}
static DEVICE_ATTR_RW(led_num);

static ssize_t led_scroll_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%d\n", vkbd_dev->scroll_lock ? 1 : 0);
}
static ssize_t led_scroll_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count)
{
    int val;
    if (kstrtoint(buf, 0, &val))
        return -EINVAL;
    vkbd_dev->scroll_lock = !!val;
    pr_info("%s: Scroll Lock LED %s\n", DRIVER_NAME, val ? "ON" : "OFF");
    return count;
}
static DEVICE_ATTR_RW(led_scroll);

/* Repeat rate (read/write via sysfs as well) */
static ssize_t repeat_delay_ms_show(struct device *dev,
                                    struct device_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%d\n", repeat_delay);
}
static ssize_t repeat_delay_ms_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf, size_t count)
{
    int val;
    if (kstrtoint(buf, 0, &val) || val < 50 || val > 2000)
        return -EINVAL;
    repeat_delay = val;
    /* Update the input device repeat parameters */
    vkbd_dev->input->rep[REP_DELAY] = val;
    pr_info("%s: Repeat delay set to %d ms\n", DRIVER_NAME, val);
    return count;
}
static DEVICE_ATTR_RW(repeat_delay_ms);

static ssize_t repeat_rate_ms_show(struct device *dev,
                                   struct device_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%d\n", repeat_rate);
}
static ssize_t repeat_rate_ms_store(struct device *dev,
                                    struct device_attribute *attr,
                                    const char *buf, size_t count)
{
    int val;
    if (kstrtoint(buf, 0, &val) || val < 10 || val > 500)
        return -EINVAL;
    repeat_rate = val;
    vkbd_dev->input->rep[REP_PERIOD] = val;
    pr_info("%s: Repeat rate set to %d ms\n", DRIVER_NAME, val);
    return count;
}
static DEVICE_ATTR_RW(repeat_rate_ms);

static struct attribute *vkbd_attrs[] = {
    &dev_attr_inject_scancode.attr,
    &dev_attr_led_caps.attr,
    &dev_attr_led_num.attr,
    &dev_attr_led_scroll.attr,
    &dev_attr_repeat_delay_ms.attr,
    &dev_attr_repeat_rate_ms.attr,
    NULL,
};

static const struct attribute_group vkbd_attr_group = {
    .attrs = vkbd_attrs,
};

/*
 * /proc/vkbd_stats - Statistics Interface
 */
static int vkbd_proc_show(struct seq_file *m, void *v)
{
    unsigned long uptime_secs;

    uptime_secs = (jiffies - vkbd_dev->start_jiffies) / HZ;

    seq_puts(m, "=== Virtual Keyboard Driver Statistics ===\n");
    seq_printf(m, "Uptime:            %lu seconds\n", uptime_secs);
    seq_printf(m, "Total Keypresses:  %lu\n", vkbd_dev->total_keypresses);
    seq_printf(m, "Total Releases:    %lu\n", vkbd_dev->total_keyreleases);
    seq_printf(m, "Buffer Overflows:  %lu\n", vkbd_dev->buffer_overflows);
    seq_printf(m, "Unknown Scancodes: %lu\n", vkbd_dev->unknown_scancodes);
    seq_printf(m, "Combos Detected:   %lu\n", vkbd_dev->combo_detections);
    seq_puts(m, "\n--- Modifier States ---\n");
    seq_printf(m, "Shift:   %s\n", vkbd_dev->shift_pressed ? "HELD" : "released");
    seq_printf(m, "Ctrl:    %s\n", vkbd_dev->ctrl_pressed ? "HELD" : "released");
    seq_printf(m, "Alt:     %s\n", vkbd_dev->alt_pressed ? "HELD" : "released");
    seq_puts(m, "\n--- LED States ---\n");
    seq_printf(m, "Caps Lock:   %s\n", vkbd_dev->caps_lock ? "ON" : "OFF");
    seq_printf(m, "Num Lock:    %s\n", vkbd_dev->num_lock ? "ON" : "OFF");
    seq_printf(m, "Scroll Lock: %s\n", vkbd_dev->scroll_lock ? "ON" : "OFF");
    seq_puts(m, "\n--- Configuration ---\n");
    seq_printf(m, "Repeat Delay: %d ms\n", repeat_delay);
    seq_printf(m, "Repeat Rate:  %d ms\n", repeat_rate);
    seq_printf(m, "Buffer Size:  %d\n", BUFFER_SIZE);

    return 0;
}

static int vkbd_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, vkbd_proc_show, NULL);
}

static const struct proc_ops vkbd_proc_ops = {
    .proc_open    = vkbd_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/*
 * Module Initialization
 */
static int __init vkbd_init(void)
{
    int ret;
    int i;
    
    pr_info("%s: Initializing virtual keyboard driver (enhanced)\n", DRIVER_NAME);
    
    /* Allocate driver data structure */
    vkbd_dev = kzalloc(sizeof(struct vkbd_device), GFP_KERNEL);
    if (!vkbd_dev)
        return -ENOMEM;
    
    /* Initialize buffer, lock, and stats */
    spin_lock_init(&vkbd_dev->buffer_lock);
    vkbd_dev->head = 0;
    vkbd_dev->tail = 0;
    vkbd_dev->shift_pressed = false;
    vkbd_dev->ctrl_pressed = false;
    vkbd_dev->alt_pressed = false;
    vkbd_dev->caps_lock = false;
    vkbd_dev->num_lock = false;
    vkbd_dev->scroll_lock = false;
    vkbd_dev->total_keypresses = 0;
    vkbd_dev->total_keyreleases = 0;
    vkbd_dev->buffer_overflows = 0;
    vkbd_dev->unknown_scancodes = 0;
    vkbd_dev->combo_detections = 0;
    vkbd_dev->start_jiffies = jiffies;

    /* Initialize tasklet for bottom-half processing */
    tasklet_init(&vkbd_dev->tasklet, vkbd_tasklet_handler,
                 (unsigned long)vkbd_dev);
    
    /* Allocate input device */
    vkbd_dev->input = input_allocate_device();
    if (!vkbd_dev->input) {
        pr_err("%s: Failed to allocate input device\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto err_free_dev;
    }
    
    /* Setup input device properties */
    vkbd_dev->input->name = "Virtual PS/2 Keyboard";
    vkbd_dev->input->phys = "virtual/input0";
    vkbd_dev->input->id.bustype = BUS_HOST;
    vkbd_dev->input->id.vendor = 0x0001;
    vkbd_dev->input->id.product = 0x0001;
    vkbd_dev->input->id.version = 0x0200;  /* Updated version */
    
    /* Set event types: key events with repeat + LED */
    vkbd_dev->input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP) | BIT_MASK(EV_LED);
    
    /* Set which keys we can generate */
    for (i = 0; i < ARRAY_SIZE(scancode_to_keycode); i++) {
        if (scancode_to_keycode[i] != 0)
            set_bit(scancode_to_keycode[i], vkbd_dev->input->keybit);
    }

    /* Set LED capabilities */
    set_bit(LED_CAPSL, vkbd_dev->input->ledbit);
    set_bit(LED_NUML, vkbd_dev->input->ledbit);
    set_bit(LED_SCROLLL, vkbd_dev->input->ledbit);

    /* Configure repeat parameters */
    vkbd_dev->input->rep[REP_DELAY] = repeat_delay;
    vkbd_dev->input->rep[REP_PERIOD] = repeat_rate;

    /* Register input device with the input subsystem */
    ret = input_register_device(vkbd_dev->input);
    if (ret) {
        pr_err("%s: Failed to register input device\n", DRIVER_NAME);
        goto err_free_input;
    }
    
    /* Create sysfs interface */
    ret = sysfs_create_group(&vkbd_dev->input->dev.kobj, &vkbd_attr_group);
    if (ret) {
        pr_err("%s: Failed to create sysfs group\n", DRIVER_NAME);
        goto err_unregister_input;
    }

    /* Create /proc/vkbd_stats */
    vkbd_proc_entry = proc_create("vkbd_stats", 0444, NULL, &vkbd_proc_ops);
    if (!vkbd_proc_entry) {
        pr_warn("%s: Failed to create /proc/vkbd_stats (non-fatal)\n", DRIVER_NAME);
    }

    pr_info("%s: Successfully registered as %s\n", DRIVER_NAME,
            dev_name(&vkbd_dev->input->dev));
    pr_info("%s: Extended scan codes: 128 entries (arrows, F11/F12, numpad, multimedia)\n",
            DRIVER_NAME);
    pr_info("%s: LEDs: Caps/Num/Scroll Lock | Repeat: %dms delay, %dms rate\n",
            DRIVER_NAME, repeat_delay, repeat_rate);
    pr_info("%s: Stats: cat /proc/vkbd_stats\n", DRIVER_NAME);
    
    return 0;

err_unregister_input:
    input_unregister_device(vkbd_dev->input);
    vkbd_dev->input = NULL;
err_free_input:
    if (vkbd_dev->input)
        input_free_device(vkbd_dev->input);
err_free_dev:
    tasklet_kill(&vkbd_dev->tasklet);
    kfree(vkbd_dev);
    return ret;
}

/*
 * Module Cleanup
 */
static void __exit vkbd_exit(void)
{
    pr_info("%s: Cleaning up virtual keyboard driver\n", DRIVER_NAME);

    /* Remove /proc entry */
    if (vkbd_proc_entry)
        proc_remove(vkbd_proc_entry);

    /* Remove sysfs interface */
    sysfs_remove_group(&vkbd_dev->input->dev.kobj, &vkbd_attr_group);
    
    /* Kill tasklet */
    tasklet_kill(&vkbd_dev->tasklet);
    
    /* Unregister input device */
    input_unregister_device(vkbd_dev->input);
    
    /* Free driver data */
    kfree(vkbd_dev);
    
    pr_info("%s: Driver unloaded (total keypresses: %lu)\n",
            DRIVER_NAME, vkbd_dev ? vkbd_dev->total_keypresses : 0);
}

module_init(vkbd_init);
module_exit(vkbd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS Course Project");
MODULE_DESCRIPTION("Virtual PS/2 Keyboard Driver - Enhanced with LEDs, /proc stats, combos, extended scan codes");
MODULE_VERSION("2.0");
