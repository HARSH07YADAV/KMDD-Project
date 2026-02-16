/*
 * touchpad_driver.c - Virtual Touchpad Driver
 *
 * Educational Linux kernel module demonstrating:
 * - Absolute positioning via EV_ABS (ABS_X, ABS_Y)
 * - Multi-touch protocol B (ABS_MT_SLOT, ABS_MT_TRACKING_ID, etc.)
 * - Single-tap and two-finger tap gestures
 * - Two-finger scroll simulation
 * - Sysfs interface for injecting touch events
 * - /proc statistics
 *
 * License: MIT
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/jiffies.h>

#define DRIVER_NAME      "virtual_touchpad"
#define TP_MAX_X         4096
#define TP_MAX_Y         4096
#define TP_MAX_PRESSURE  255
#define TP_MAX_SLOTS     5   /* Up to 5 simultaneous fingers */

/* Driver data structure */
struct vtp_device {
    struct input_dev *input;

    /* Statistics */
    unsigned long total_touches;
    unsigned long total_taps;
    unsigned long total_two_finger_taps;
    unsigned long total_scrolls;
    unsigned long total_moves;
    unsigned long start_jiffies;
};

static struct vtp_device *vtp_dev;
static struct proc_dir_entry *vtp_proc_entry;

/*
 * Sysfs: inject_touch - Single finger touch event
 * Format: "x y pressure"  (pressure=0 means lift)
 */
static ssize_t inject_touch_store(struct device *dev,
                                   struct device_attribute *attr,
                                   const char *buf, size_t count)
{
    int x, y, pressure;

    if (sscanf(buf, "%d %d %d", &x, &y, &pressure) != 3)
        return -EINVAL;

    if (x < 0 || x > TP_MAX_X || y < 0 || y > TP_MAX_Y ||
        pressure < 0 || pressure > TP_MAX_PRESSURE)
        return -EINVAL;

    /* Report multi-touch slot 0 */
    input_mt_slot(vtp_dev->input, 0);

    if (pressure > 0) {
        input_mt_report_slot_state(vtp_dev->input, MT_TOOL_FINGER, true);
        input_report_abs(vtp_dev->input, ABS_MT_POSITION_X, x);
        input_report_abs(vtp_dev->input, ABS_MT_POSITION_Y, y);
        input_report_abs(vtp_dev->input, ABS_MT_PRESSURE, pressure);

        /* Also report single-touch for non-MT-aware apps */
        input_report_abs(vtp_dev->input, ABS_X, x);
        input_report_abs(vtp_dev->input, ABS_Y, y);
        input_report_abs(vtp_dev->input, ABS_PRESSURE, pressure);
        input_report_key(vtp_dev->input, BTN_TOUCH, 1);

        vtp_dev->total_moves++;
    } else {
        /* Finger lifted */
        input_mt_report_slot_state(vtp_dev->input, MT_TOOL_FINGER, false);
        input_report_key(vtp_dev->input, BTN_TOUCH, 0);
        input_report_abs(vtp_dev->input, ABS_PRESSURE, 0);
        vtp_dev->total_touches++;
    }

    input_mt_sync_frame(vtp_dev->input);
    input_sync(vtp_dev->input);

    pr_debug("%s: Touch x=%d y=%d pressure=%d\n", DRIVER_NAME, x, y, pressure);
    return count;
}
static DEVICE_ATTR_WO(inject_touch);

/*
 * Sysfs: inject_tap - Simulate a single-finger tap
 * Format: "x y"
 */
static ssize_t inject_tap_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf, size_t count)
{
    int x, y;

    if (sscanf(buf, "%d %d", &x, &y) != 2)
        return -EINVAL;

    if (x < 0 || x > TP_MAX_X || y < 0 || y > TP_MAX_Y)
        return -EINVAL;

    /* Touch down */
    input_mt_slot(vtp_dev->input, 0);
    input_mt_report_slot_state(vtp_dev->input, MT_TOOL_FINGER, true);
    input_report_abs(vtp_dev->input, ABS_MT_POSITION_X, x);
    input_report_abs(vtp_dev->input, ABS_MT_POSITION_Y, y);
    input_report_abs(vtp_dev->input, ABS_MT_PRESSURE, 128);
    input_report_abs(vtp_dev->input, ABS_X, x);
    input_report_abs(vtp_dev->input, ABS_Y, y);
    input_report_abs(vtp_dev->input, ABS_PRESSURE, 128);
    input_report_key(vtp_dev->input, BTN_TOUCH, 1);
    input_report_key(vtp_dev->input, BTN_LEFT, 1);
    input_mt_sync_frame(vtp_dev->input);
    input_sync(vtp_dev->input);

    /* Touch up */
    input_mt_slot(vtp_dev->input, 0);
    input_mt_report_slot_state(vtp_dev->input, MT_TOOL_FINGER, false);
    input_report_key(vtp_dev->input, BTN_TOUCH, 0);
    input_report_key(vtp_dev->input, BTN_LEFT, 0);
    input_report_abs(vtp_dev->input, ABS_PRESSURE, 0);
    input_mt_sync_frame(vtp_dev->input);
    input_sync(vtp_dev->input);

    vtp_dev->total_taps++;
    pr_info("%s: Tap at (%d, %d)\n", DRIVER_NAME, x, y);
    return count;
}
static DEVICE_ATTR_WO(inject_tap);

/*
 * Sysfs: inject_two_finger_tap - Simulate a two-finger tap (right-click)
 * Format: "x1 y1 x2 y2"
 */
static ssize_t inject_two_finger_tap_store(struct device *dev,
                                            struct device_attribute *attr,
                                            const char *buf, size_t count)
{
    int x1, y1, x2, y2;

    if (sscanf(buf, "%d %d %d %d", &x1, &y1, &x2, &y2) != 4)
        return -EINVAL;

    /* Two fingers down */
    input_mt_slot(vtp_dev->input, 0);
    input_mt_report_slot_state(vtp_dev->input, MT_TOOL_FINGER, true);
    input_report_abs(vtp_dev->input, ABS_MT_POSITION_X, x1);
    input_report_abs(vtp_dev->input, ABS_MT_POSITION_Y, y1);
    input_report_abs(vtp_dev->input, ABS_MT_PRESSURE, 128);

    input_mt_slot(vtp_dev->input, 1);
    input_mt_report_slot_state(vtp_dev->input, MT_TOOL_FINGER, true);
    input_report_abs(vtp_dev->input, ABS_MT_POSITION_X, x2);
    input_report_abs(vtp_dev->input, ABS_MT_POSITION_Y, y2);
    input_report_abs(vtp_dev->input, ABS_MT_PRESSURE, 128);

    input_report_key(vtp_dev->input, BTN_TOUCH, 1);
    input_report_key(vtp_dev->input, BTN_RIGHT, 1);
    input_mt_sync_frame(vtp_dev->input);
    input_sync(vtp_dev->input);

    /* Both fingers up */
    input_mt_slot(vtp_dev->input, 0);
    input_mt_report_slot_state(vtp_dev->input, MT_TOOL_FINGER, false);
    input_mt_slot(vtp_dev->input, 1);
    input_mt_report_slot_state(vtp_dev->input, MT_TOOL_FINGER, false);
    input_report_key(vtp_dev->input, BTN_TOUCH, 0);
    input_report_key(vtp_dev->input, BTN_RIGHT, 0);
    input_mt_sync_frame(vtp_dev->input);
    input_sync(vtp_dev->input);

    vtp_dev->total_two_finger_taps++;
    pr_info("%s: Two-finger tap at (%d,%d) (%d,%d)\n",
            DRIVER_NAME, x1, y1, x2, y2);
    return count;
}
static DEVICE_ATTR_WO(inject_two_finger_tap);

/*
 * Sysfs: inject_scroll - Simulate two-finger scroll
 * Format: "dx dy"  (positive dy = scroll down, negative = scroll up)
 */
static ssize_t inject_scroll_store(struct device *dev,
                                    struct device_attribute *attr,
                                    const char *buf, size_t count)
{
    int dx, dy;

    if (sscanf(buf, "%d %d", &dx, &dy) != 2)
        return -EINVAL;

    /* Two-finger scroll generates REL_WHEEL / REL_HWHEEL */
    if (dy != 0)
        input_report_rel(vtp_dev->input, REL_WHEEL, -dy);  /* inverted for natural scroll */
    if (dx != 0)
        input_report_rel(vtp_dev->input, REL_HWHEEL, dx);
    input_sync(vtp_dev->input);

    vtp_dev->total_scrolls++;
    pr_debug("%s: Scroll dx=%d dy=%d\n", DRIVER_NAME, dx, dy);
    return count;
}
static DEVICE_ATTR_WO(inject_scroll);

static struct attribute *vtp_attrs[] = {
    &dev_attr_inject_touch.attr,
    &dev_attr_inject_tap.attr,
    &dev_attr_inject_two_finger_tap.attr,
    &dev_attr_inject_scroll.attr,
    NULL,
};

static const struct attribute_group vtp_attr_group = {
    .attrs = vtp_attrs,
};

/*
 * /proc/vtouchpad_stats
 */
static int vtp_proc_show(struct seq_file *m, void *v)
{
    unsigned long uptime_secs;

    uptime_secs = (jiffies - vtp_dev->start_jiffies) / HZ;

    seq_puts(m, "=== Virtual Touchpad Driver Statistics ===\n");
    seq_printf(m, "Uptime:              %lu seconds\n", uptime_secs);
    seq_printf(m, "Resolution:          %d x %d\n", TP_MAX_X, TP_MAX_Y);
    seq_printf(m, "Max Slots:           %d\n", TP_MAX_SLOTS);
    seq_puts(m, "\n--- Touch Statistics ---\n");
    seq_printf(m, "Total Touches:       %lu\n", vtp_dev->total_touches);
    seq_printf(m, "Total Moves:         %lu\n", vtp_dev->total_moves);
    seq_printf(m, "Single Taps:         %lu\n", vtp_dev->total_taps);
    seq_printf(m, "Two-Finger Taps:     %lu\n", vtp_dev->total_two_finger_taps);
    seq_printf(m, "Scroll Events:       %lu\n", vtp_dev->total_scrolls);

    return 0;
}

static int vtp_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, vtp_proc_show, NULL);
}

static const struct proc_ops vtp_proc_ops = {
    .proc_open    = vtp_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/*
 * Module Initialization
 */
static int __init vtp_init(void)
{
    int ret;

    pr_info("%s: Initializing virtual touchpad driver\n", DRIVER_NAME);

    vtp_dev = kzalloc(sizeof(struct vtp_device), GFP_KERNEL);
    if (!vtp_dev)
        return -ENOMEM;

    vtp_dev->start_jiffies = jiffies;

    /* Allocate input device */
    vtp_dev->input = input_allocate_device();
    if (!vtp_dev->input) {
        pr_err("%s: Failed to allocate input device\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto err_free_dev;
    }

    vtp_dev->input->name = "Virtual Touchpad";
    vtp_dev->input->phys = "virtual/input2";
    vtp_dev->input->id.bustype = BUS_HOST;
    vtp_dev->input->id.vendor  = 0x0001;
    vtp_dev->input->id.product = 0x0003;
    vtp_dev->input->id.version = 0x0100;

    /* Event types: keys, absolute axes, relative (for scroll) */
    set_bit(EV_KEY, vtp_dev->input->evbit);
    set_bit(EV_ABS, vtp_dev->input->evbit);
    set_bit(EV_REL, vtp_dev->input->evbit);

    /* Buttons */
    set_bit(BTN_TOUCH, vtp_dev->input->keybit);
    set_bit(BTN_LEFT, vtp_dev->input->keybit);
    set_bit(BTN_RIGHT, vtp_dev->input->keybit);
    set_bit(BTN_TOOL_FINGER, vtp_dev->input->keybit);
    set_bit(BTN_TOOL_DOUBLETAP, vtp_dev->input->keybit);

    /* Scroll axes */
    set_bit(REL_WHEEL, vtp_dev->input->relbit);
    set_bit(REL_HWHEEL, vtp_dev->input->relbit);

    /* Single-touch absolute axes */
    input_set_abs_params(vtp_dev->input, ABS_X, 0, TP_MAX_X, 0, 0);
    input_set_abs_params(vtp_dev->input, ABS_Y, 0, TP_MAX_Y, 0, 0);
    input_set_abs_params(vtp_dev->input, ABS_PRESSURE, 0, TP_MAX_PRESSURE, 0, 0);

    /* Multi-touch protocol B */
    input_mt_init_slots(vtp_dev->input, TP_MAX_SLOTS, INPUT_MT_POINTER);
    input_set_abs_params(vtp_dev->input, ABS_MT_POSITION_X, 0, TP_MAX_X, 0, 0);
    input_set_abs_params(vtp_dev->input, ABS_MT_POSITION_Y, 0, TP_MAX_Y, 0, 0);
    input_set_abs_params(vtp_dev->input, ABS_MT_PRESSURE, 0, TP_MAX_PRESSURE, 0, 0);

    /* Register input device */
    ret = input_register_device(vtp_dev->input);
    if (ret) {
        pr_err("%s: Failed to register input device\n", DRIVER_NAME);
        goto err_free_input;
    }

    /* Sysfs */
    ret = sysfs_create_group(&vtp_dev->input->dev.kobj, &vtp_attr_group);
    if (ret) {
        pr_err("%s: Failed to create sysfs group\n", DRIVER_NAME);
        goto err_unregister;
    }

    /* /proc */
    vtp_proc_entry = proc_create("vtouchpad_stats", 0444, NULL, &vtp_proc_ops);
    if (!vtp_proc_entry)
        pr_warn("%s: Failed to create /proc/vtouchpad_stats (non-fatal)\n", DRIVER_NAME);

    pr_info("%s: Registered as %s (%dx%d, %d slots)\n",
            DRIVER_NAME, dev_name(&vtp_dev->input->dev),
            TP_MAX_X, TP_MAX_Y, TP_MAX_SLOTS);
    pr_info("%s: Sysfs: inject_touch, inject_tap, inject_two_finger_tap, inject_scroll\n",
            DRIVER_NAME);
    pr_info("%s: Stats: cat /proc/vtouchpad_stats\n", DRIVER_NAME);

    return 0;

err_unregister:
    input_unregister_device(vtp_dev->input);
    vtp_dev->input = NULL;
err_free_input:
    if (vtp_dev->input)
        input_free_device(vtp_dev->input);
err_free_dev:
    kfree(vtp_dev);
    return ret;
}

/*
 * Module Cleanup
 */
static void __exit vtp_exit(void)
{
    pr_info("%s: Cleaning up virtual touchpad driver\n", DRIVER_NAME);

    if (vtp_proc_entry)
        proc_remove(vtp_proc_entry);

    sysfs_remove_group(&vtp_dev->input->dev.kobj, &vtp_attr_group);
    input_unregister_device(vtp_dev->input);
    kfree(vtp_dev);

    pr_info("%s: Driver unloaded\n", DRIVER_NAME);
}

module_init(vtp_init);
module_exit(vtp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS Course Project");
MODULE_DESCRIPTION("Virtual Touchpad Driver - Multi-touch, gestures, scroll simulation");
MODULE_VERSION("1.0");
