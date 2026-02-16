/*
 * mouse_driver.c - Virtual PS/2 Mouse Driver (Enhanced)
 * 
 * Educational Linux kernel module demonstrating:
 * - Input subsystem integration for mouse events
 * - PS/2 3-byte and 4-byte (IntelliMouse) packet parsing
 * - Scroll wheel support (REL_WHEEL)
 * - Extended buttons (Side/Forward)
 * - Configurable DPI/sensitivity via sysfs
 * - /proc statistics interface
 * - IRQ simulation with tasklets
 * - Proper locking and buffering
 *
 * PS/2 Standard Packet (3 bytes):
 * Byte 0: [Y_overflow | X_overflow | Y_sign | X_sign | 1 | Middle | Right | Left]
 * Byte 1: X movement (8-bit signed)
 * Byte 2: Y movement (8-bit signed)
 *
 * IntelliMouse Packet (4 bytes) - adds:
 * Byte 3: Scroll wheel (4-bit signed) | Buttons 4+5 in bits 4-5
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

#define DRIVER_NAME "virtual_mouse"
#define BUFFER_SIZE 512
#define PACKET_SIZE_STANDARD 3
#define PACKET_SIZE_INTELLIMOUSE 4

/* Module parameters */
static int dpi_multiplier = 100;  /* percentage, 100 = 1:1 */
module_param(dpi_multiplier, int, 0644);
MODULE_PARM_DESC(dpi_multiplier, "Mouse DPI/sensitivity multiplier as percentage (default: 100)");

static bool intellimouse_mode = true;
module_param(intellimouse_mode, bool, 0644);
MODULE_PARM_DESC(intellimouse_mode, "Enable 4-byte IntelliMouse packets with scroll (default: true)");

/* Driver data structure */
struct vmouse_device {
    struct input_dev *input;
    struct tasklet_struct tasklet;
    spinlock_t buffer_lock;
    unsigned char buffer[BUFFER_SIZE];
    unsigned int head;
    unsigned int tail;
    unsigned char packet[PACKET_SIZE_INTELLIMOUSE];
    unsigned int packet_idx;
    unsigned int current_packet_size;

    /* Statistics */
    unsigned long total_packets;
    unsigned long total_clicks;
    unsigned long left_clicks;
    unsigned long right_clicks;
    unsigned long middle_clicks;
    unsigned long side_clicks;
    unsigned long forward_clicks;
    unsigned long scroll_events;
    long total_dx;
    long total_dy;
    unsigned long total_distance;  /* approximate Manhattan distance */
    unsigned long buffer_overflows;
    unsigned long invalid_packets;
    unsigned long start_jiffies;
};

static struct vmouse_device *vmouse_dev;
static struct proc_dir_entry *vmouse_proc_entry;

/*
 * PS/2 Packet Bit Definitions
 */
#define PS2_LEFT_BTN    (1 << 0)
#define PS2_RIGHT_BTN   (1 << 1)
#define PS2_MIDDLE_BTN  (1 << 2)
#define PS2_ALWAYS_ONE  (1 << 3)
#define PS2_X_SIGN      (1 << 4)
#define PS2_Y_SIGN      (1 << 5)
#define PS2_X_OVERFLOW  (1 << 6)
#define PS2_Y_OVERFLOW  (1 << 7)

/* IntelliMouse byte 3 */
#define IM_WHEEL_MASK  0x0F
#define IM_BTN4        (1 << 4)
#define IM_BTN5        (1 << 5)

/*
 * Buffer Management Functions
 */
static bool buffer_empty(struct vmouse_device *dev)
{
    return dev->head == dev->tail;
}

static bool buffer_full(struct vmouse_device *dev)
{
    return ((dev->head + 1) % BUFFER_SIZE) == dev->tail;
}

static void buffer_push(struct vmouse_device *dev, unsigned char byte)
{
    unsigned long flags;
    
    spin_lock_irqsave(&dev->buffer_lock, flags);
    
    if (!buffer_full(dev)) {
        dev->buffer[dev->head] = byte;
        dev->head = (dev->head + 1) % BUFFER_SIZE;
    } else {
        dev->buffer_overflows++;
        pr_warn_ratelimited("%s: Buffer overflow (#%lu), dropping byte 0x%02x\n",
                DRIVER_NAME, dev->buffer_overflows, byte);
    }
    
    spin_unlock_irqrestore(&dev->buffer_lock, flags);
}

static int buffer_pop(struct vmouse_device *dev, unsigned char *byte)
{
    unsigned long flags;
    int ret = 0;
    
    spin_lock_irqsave(&dev->buffer_lock, flags);
    
    if (!buffer_empty(dev)) {
        *byte = dev->buffer[dev->tail];
        dev->tail = (dev->tail + 1) % BUFFER_SIZE;
        ret = 1;
    }
    
    spin_unlock_irqrestore(&dev->buffer_lock, flags);
    
    return ret;
}

/*
 * Apply DPI scaling
 */
static int apply_dpi(int value)
{
    return (value * dpi_multiplier) / 100;
}

/*
 * Parse and Process PS/2 Mouse Packet (standard or IntelliMouse)
 */
static bool process_packet(struct vmouse_device *dev)
{
    unsigned char status = dev->packet[0];
    signed char dx_raw = (signed char)dev->packet[1];
    signed char dy_raw = (signed char)dev->packet[2];
    int dx, dy;
    bool left, right, middle, btn_side, btn_forward;
    signed char scroll = 0;
    
    /* Validate packet - bit 3 should always be 1 */
    if (!(status & PS2_ALWAYS_ONE)) {
        dev->invalid_packets++;
        pr_debug("%s: Invalid packet - bit 3 not set\n", DRIVER_NAME);
        return false;
    }
    
    /* Check for overflow */
    if (status & PS2_X_OVERFLOW)
        pr_debug("%s: X overflow detected\n", DRIVER_NAME);
    if (status & PS2_Y_OVERFLOW)
        pr_debug("%s: Y overflow detected\n", DRIVER_NAME);
    
    /* Extract button states */
    left = (status & PS2_LEFT_BTN) != 0;
    right = (status & PS2_RIGHT_BTN) != 0;
    middle = (status & PS2_MIDDLE_BTN) != 0;
    btn_side = false;
    btn_forward = false;

    /* Calculate relative movement with DPI scaling */
    dx = apply_dpi(dx_raw);
    dy = apply_dpi(dy_raw);
    
    /* PS/2 Y axis is inverted compared to Linux convention */
    dy = -dy;

    /* Process IntelliMouse extra byte */
    if (dev->current_packet_size == PACKET_SIZE_INTELLIMOUSE) {
        unsigned char extra = dev->packet[3];

        /* Scroll wheel (lower 4 bits, sign-extended) */
        scroll = (signed char)((extra & IM_WHEEL_MASK) |
                 ((extra & 0x08) ? 0xF0 : 0x00));

        /* Extended buttons */
        btn_side = (extra & IM_BTN4) != 0;
        btn_forward = (extra & IM_BTN5) != 0;
    }

    /* Update statistics */
    dev->total_packets++;
    if (left) { dev->left_clicks++; dev->total_clicks++; }
    if (right) { dev->right_clicks++; dev->total_clicks++; }
    if (middle) { dev->middle_clicks++; dev->total_clicks++; }
    if (btn_side) { dev->side_clicks++; dev->total_clicks++; }
    if (btn_forward) { dev->forward_clicks++; dev->total_clicks++; }
    if (scroll != 0) dev->scroll_events++;
    dev->total_dx += dx;
    dev->total_dy += dy;
    dev->total_distance += abs(dx) + abs(dy);

    pr_debug("%s: Pkt: btns[L:%d R:%d M:%d S:%d F:%d] dx:%d dy:%d scroll:%d\n",
             DRIVER_NAME, left, right, middle, btn_side, btn_forward, dx, dy, scroll);
    
    /* Report button events */
    input_report_key(dev->input, BTN_LEFT, left);
    input_report_key(dev->input, BTN_RIGHT, right);
    input_report_key(dev->input, BTN_MIDDLE, middle);
    input_report_key(dev->input, BTN_SIDE, btn_side);
    input_report_key(dev->input, BTN_EXTRA, btn_forward);
    
    /* Report relative motion */
    if (dx != 0)
        input_report_rel(dev->input, REL_X, dx);
    if (dy != 0)
        input_report_rel(dev->input, REL_Y, dy);
    if (scroll != 0)
        input_report_rel(dev->input, REL_WHEEL, scroll);
    
    /* Sync to indicate complete event */
    input_sync(dev->input);
    
    return true;
}

/*
 * Tasklet Bottom-Half Handler
 */
static void vmouse_tasklet_handler(unsigned long data)
{
    struct vmouse_device *dev = (struct vmouse_device *)data;
    unsigned char byte;
    
    while (buffer_pop(dev, &byte)) {
        dev->packet[dev->packet_idx++] = byte;
        
        if (dev->packet_idx >= dev->current_packet_size) {
            process_packet(dev);
            dev->packet_idx = 0;
        }
    }
}

/*
 * Simulated IRQ Handler (Top Half)
 */
static void vmouse_simulate_irq(unsigned char byte)
{
    buffer_push(vmouse_dev, byte);
    tasklet_schedule(&vmouse_dev->tasklet);
}

/*
 * Sysfs Interfaces
 */

/* Inject packet (3 or 4 bytes) */
static ssize_t inject_packet_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf, size_t count)
{
    unsigned long bytes[4];
    int i, n;
    const char *p = buf;
    char *endp;
    int expected = vmouse_dev->current_packet_size;
    
    for (i = 0; i < expected; i++) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n'))
            p++;
        
        if (!*p)
            break;
        
        bytes[i] = simple_strtoul(p, &endp, 0);
        if (endp == p) {
            pr_warn("%s: Invalid packet format\n", DRIVER_NAME);
            return -EINVAL;
        }
        
        if (bytes[i] > 0xFF) {
            pr_warn("%s: Invalid byte value 0x%lx\n", DRIVER_NAME, bytes[i]);
            return -EINVAL;
        }
        
        p = endp;
    }
    
    n = i;
    
    /* Accept both 3-byte and 4-byte packets */
    if (n != 3 && n != 4) {
        pr_warn("%s: Expected 3 or 4 bytes, got %d\n", DRIVER_NAME, n);
        return -EINVAL;
    }

    /* Temporarily adjust packet size if 3-byte packet in intellimouse mode */
    if (n == 3 && vmouse_dev->current_packet_size == PACKET_SIZE_INTELLIMOUSE) {
        unsigned int saved = vmouse_dev->current_packet_size;
        vmouse_dev->current_packet_size = PACKET_SIZE_STANDARD;
        for (i = 0; i < n; i++)
            vmouse_simulate_irq((unsigned char)bytes[i]);
        /* Wait for tasklet to process then restore */
        tasklet_disable(&vmouse_dev->tasklet);
        tasklet_enable(&vmouse_dev->tasklet);
        vmouse_dev->current_packet_size = saved;
    } else {
        for (i = 0; i < n; i++)
            vmouse_simulate_irq((unsigned char)bytes[i]);
    }
    
    return count;
}
static DEVICE_ATTR_WO(inject_packet);

/* DPI/sensitivity (read/write) */
static ssize_t dpi_show(struct device *dev,
                        struct device_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%d\n", dpi_multiplier);
}
static ssize_t dpi_store(struct device *dev,
                         struct device_attribute *attr,
                         const char *buf, size_t count)
{
    int val;
    if (kstrtoint(buf, 0, &val) || val < 10 || val > 1000)
        return -EINVAL;
    dpi_multiplier = val;
    pr_info("%s: DPI multiplier set to %d%%\n", DRIVER_NAME, val);
    return count;
}
static DEVICE_ATTR_RW(dpi);

/* IntelliMouse mode toggle */
static ssize_t intellimouse_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%d\n", intellimouse_mode ? 1 : 0);
}
static ssize_t intellimouse_store(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf, size_t count)
{
    int val;
    if (kstrtoint(buf, 0, &val))
        return -EINVAL;
    intellimouse_mode = !!val;
    vmouse_dev->current_packet_size = intellimouse_mode ?
        PACKET_SIZE_INTELLIMOUSE : PACKET_SIZE_STANDARD;
    vmouse_dev->packet_idx = 0;  /* Reset packet assembly */
    pr_info("%s: IntelliMouse mode %s (%d-byte packets)\n",
            DRIVER_NAME, val ? "enabled" : "disabled",
            vmouse_dev->current_packet_size);
    return count;
}
static DEVICE_ATTR_RW(intellimouse);

static struct attribute *vmouse_attrs[] = {
    &dev_attr_inject_packet.attr,
    &dev_attr_dpi.attr,
    &dev_attr_intellimouse.attr,
    NULL,
};

static const struct attribute_group vmouse_attr_group = {
    .attrs = vmouse_attrs,
};

/*
 * /proc/vmouse_stats - Statistics Interface
 */
static int vmouse_proc_show(struct seq_file *m, void *v)
{
    unsigned long uptime_secs;

    uptime_secs = (jiffies - vmouse_dev->start_jiffies) / HZ;

    seq_puts(m, "=== Virtual Mouse Driver Statistics ===\n");
    seq_printf(m, "Uptime:              %lu seconds\n", uptime_secs);
    seq_printf(m, "Packet Mode:         %s (%d bytes)\n",
               intellimouse_mode ? "IntelliMouse" : "Standard",
               vmouse_dev->current_packet_size);
    seq_printf(m, "DPI Multiplier:      %d%%\n", dpi_multiplier);
    seq_puts(m, "\n--- Packet Statistics ---\n");
    seq_printf(m, "Total Packets:       %lu\n", vmouse_dev->total_packets);
    seq_printf(m, "Invalid Packets:     %lu\n", vmouse_dev->invalid_packets);
    seq_printf(m, "Buffer Overflows:    %lu\n", vmouse_dev->buffer_overflows);
    seq_puts(m, "\n--- Button Clicks ---\n");
    seq_printf(m, "Total Clicks:        %lu\n", vmouse_dev->total_clicks);
    seq_printf(m, "  Left:              %lu\n", vmouse_dev->left_clicks);
    seq_printf(m, "  Right:             %lu\n", vmouse_dev->right_clicks);
    seq_printf(m, "  Middle:            %lu\n", vmouse_dev->middle_clicks);
    seq_printf(m, "  Side:              %lu\n", vmouse_dev->side_clicks);
    seq_printf(m, "  Forward:           %lu\n", vmouse_dev->forward_clicks);
    seq_puts(m, "\n--- Movement ---\n");
    seq_printf(m, "Total dX:            %ld\n", vmouse_dev->total_dx);
    seq_printf(m, "Total dY:            %ld\n", vmouse_dev->total_dy);
    seq_printf(m, "Total Distance:      %lu units\n", vmouse_dev->total_distance);
    seq_printf(m, "Scroll Events:       %lu\n", vmouse_dev->scroll_events);

    return 0;
}

static int vmouse_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, vmouse_proc_show, NULL);
}

static const struct proc_ops vmouse_proc_ops = {
    .proc_open    = vmouse_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/*
 * Module Initialization
 */
static int __init vmouse_init(void)
{
    int ret;
    
    pr_info("%s: Initializing virtual mouse driver (enhanced)\n", DRIVER_NAME);
    
    vmouse_dev = kzalloc(sizeof(struct vmouse_device), GFP_KERNEL);
    if (!vmouse_dev)
        return -ENOMEM;
    
    /* Initialize buffer, lock, and stats */
    spin_lock_init(&vmouse_dev->buffer_lock);
    vmouse_dev->head = 0;
    vmouse_dev->tail = 0;
    vmouse_dev->packet_idx = 0;
    vmouse_dev->current_packet_size = intellimouse_mode ?
        PACKET_SIZE_INTELLIMOUSE : PACKET_SIZE_STANDARD;
    vmouse_dev->start_jiffies = jiffies;

    /* Initialize tasklet */
    tasklet_init(&vmouse_dev->tasklet, vmouse_tasklet_handler,
                 (unsigned long)vmouse_dev);
    
    /* Allocate input device */
    vmouse_dev->input = input_allocate_device();
    if (!vmouse_dev->input) {
        pr_err("%s: Failed to allocate input device\n", DRIVER_NAME);
        ret = -ENOMEM;
        goto err_free_dev;
    }
    
    /* Setup input device properties */
    vmouse_dev->input->name = "Virtual PS/2 Mouse";
    vmouse_dev->input->phys = "virtual/input1";
    vmouse_dev->input->id.bustype = BUS_HOST;
    vmouse_dev->input->id.vendor = 0x0001;
    vmouse_dev->input->id.product = 0x0002;
    vmouse_dev->input->id.version = 0x0200;  /* Updated version */
    
    /* Set event types: relative positioning and buttons */
    vmouse_dev->input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
    
    /* Set which buttons we support */
    set_bit(BTN_LEFT, vmouse_dev->input->keybit);
    set_bit(BTN_RIGHT, vmouse_dev->input->keybit);
    set_bit(BTN_MIDDLE, vmouse_dev->input->keybit);
    set_bit(BTN_SIDE, vmouse_dev->input->keybit);
    set_bit(BTN_EXTRA, vmouse_dev->input->keybit);
    
    /* Set relative axes (including scroll wheel) */
    set_bit(REL_X, vmouse_dev->input->relbit);
    set_bit(REL_Y, vmouse_dev->input->relbit);
    set_bit(REL_WHEEL, vmouse_dev->input->relbit);
    set_bit(REL_HWHEEL, vmouse_dev->input->relbit);  /* Horizontal scroll */
    
    /* Register input device */
    ret = input_register_device(vmouse_dev->input);
    if (ret) {
        pr_err("%s: Failed to register input device\n", DRIVER_NAME);
        goto err_free_input;
    }
    
    /* Create sysfs interface */
    ret = sysfs_create_group(&vmouse_dev->input->dev.kobj, &vmouse_attr_group);
    if (ret) {
        pr_err("%s: Failed to create sysfs group\n", DRIVER_NAME);
        goto err_unregister_input;
    }

    /* Create /proc/vmouse_stats */
    vmouse_proc_entry = proc_create("vmouse_stats", 0444, NULL, &vmouse_proc_ops);
    if (!vmouse_proc_entry)
        pr_warn("%s: Failed to create /proc/vmouse_stats (non-fatal)\n", DRIVER_NAME);

    pr_info("%s: Successfully registered as %s\n", DRIVER_NAME,
            dev_name(&vmouse_dev->input->dev));
    pr_info("%s: Mode: %s (%d-byte packets) | DPI: %d%%\n",
            DRIVER_NAME,
            intellimouse_mode ? "IntelliMouse (scroll+side buttons)" : "Standard",
            vmouse_dev->current_packet_size, dpi_multiplier);
    pr_info("%s: Buttons: Left/Right/Middle/Side/Forward | Scroll: Vertical+Horizontal\n",
            DRIVER_NAME);
    pr_info("%s: Stats: cat /proc/vmouse_stats\n", DRIVER_NAME);
    
    return 0;

err_unregister_input:
    input_unregister_device(vmouse_dev->input);
    vmouse_dev->input = NULL;
err_free_input:
    if (vmouse_dev->input)
        input_free_device(vmouse_dev->input);
err_free_dev:
    tasklet_kill(&vmouse_dev->tasklet);
    kfree(vmouse_dev);
    return ret;
}

/*
 * Module Cleanup
 */
static void __exit vmouse_exit(void)
{
    pr_info("%s: Cleaning up virtual mouse driver\n", DRIVER_NAME);

    /* Remove /proc entry */
    if (vmouse_proc_entry)
        proc_remove(vmouse_proc_entry);

    /* Remove sysfs interface */
    sysfs_remove_group(&vmouse_dev->input->dev.kobj, &vmouse_attr_group);
    
    /* Kill tasklet */
    tasklet_kill(&vmouse_dev->tasklet);
    
    /* Unregister input device */
    input_unregister_device(vmouse_dev->input);
    
    /* Free driver data */
    kfree(vmouse_dev);
    
    pr_info("%s: Driver unloaded\n", DRIVER_NAME);
}

module_init(vmouse_init);
module_exit(vmouse_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS Course Project");
MODULE_DESCRIPTION("Virtual PS/2 Mouse Driver - Enhanced with scroll, DPI, side buttons, /proc stats");
MODULE_VERSION("2.0");
