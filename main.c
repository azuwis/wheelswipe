/*
 * Listen to mouse horizontal scroll wheel events, simulate touchpad two-finger swipe in real-time
 * - grab mouse device, block REL_HWHEEL/REL_HWHEEL_HI_RES
 * - forward other mouse events to virtual mouse device
 * - convert horizontal scroll events to two-finger swipe
 *
 * Environment variables:
 *   IDLE_TIMEOUT_MS        - timeout to release touch (milliseconds), default 500
 *   SCROLL_TO_PIXEL_RATIO  - scroll value to pixel ratio, default -1
 *
 * Compile: gcc -o hwheel2swipe main.c -lpthread
 * Run: sudo ./hwheel2swipe /dev/input/event7
 *      IDLE_TIMEOUT_MS=300 SCROLL_TO_PIXEL_RATIO=-2 sudo -E ./hwheel2swipe /dev/input/event7
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/time.h>

/* Touchpad parameters */
#define TOUCHPAD_MAX_X 1919
#define TOUCHPAD_MAX_Y 1079
#define TOUCHPAD_MAX_TRACKING_ID 65535

/* Two-finger position parameters */
#define FINGER1_Y 400
#define FINGER2_Y 600
#define FINGER_OFFSET_X 200
#define CENTER_X (TOUCHPAD_MAX_X / 2)

/* Default parameters */
#define DEFAULT_IDLE_TIMEOUT_MS 500
#define DEFAULT_SCROLL_TO_PIXEL_RATIO (-1)

/* Runtime parameters */
static int idle_timeout_us;
static int scroll_to_pixel_ratio;

/* Global state */
static int mouse_fd = -1;
static int virtual_mouse_fd = -1;
static int virtual_touchpad_fd = -1;

static int tracking_id_base = 0;
static int is_touching = 0;
static int finger1_x;
static int finger2_x;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t timeout_thread;
static int timeout_thread_running = 0;
static struct timeval last_scroll_time;
static int pending_release = 0;
static volatile int running = 1;

/* Load environment variable configuration */
static void load_config(void)
{
    char *env_val;

    /* Set default values */
    idle_timeout_us = DEFAULT_IDLE_TIMEOUT_MS * 1000;
    scroll_to_pixel_ratio = DEFAULT_SCROLL_TO_PIXEL_RATIO;
    finger1_x = CENTER_X - FINGER_OFFSET_X;
    finger2_x = CENTER_X + FINGER_OFFSET_X;

    env_val = getenv("IDLE_TIMEOUT_MS");
    if (env_val) {
        int val = atoi(env_val);
        if (val > 0) {
            idle_timeout_us = val * 1000;
            printf("IDLE_TIMEOUT_MS = %d\n", val);
        }
    }

    env_val = getenv("SCROLL_TO_PIXEL_RATIO");
    if (env_val) {
        int val = atoi(env_val);
        if (val != 0) {
            scroll_to_pixel_ratio = val;
            printf("SCROLL_TO_PIXEL_RATIO = %d\n", val);
        }
    }
}

/* Send event */
static void send_event(int fd, int type, int code, int value)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    gettimeofday(&ev.time, NULL);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    write(fd, &ev, sizeof(ev));
}

/* Send sync event */
static void send_sync(int fd)
{
    send_event(fd, EV_SYN, SYN_REPORT, 0);
}

/* Start two-finger touch */
static void start_touch(void)
{
    int id1 = tracking_id_base;
    int id2 = tracking_id_base + 1;
    tracking_id_base = (tracking_id_base + 2) % (TOUCHPAD_MAX_TRACKING_ID - 1);

    /* Reset position to center */
    finger1_x = CENTER_X - FINGER_OFFSET_X;
    finger2_x = CENTER_X + FINGER_OFFSET_X;

    /* Finger 1 */
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_SLOT, 0);
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_TRACKING_ID, id1);
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_POSITION_X, finger1_x);
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_POSITION_Y, FINGER1_Y);

    /* Finger 2 */
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_SLOT, 1);
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_TRACKING_ID, id2);
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_POSITION_X, finger2_x);
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_POSITION_Y, FINGER2_Y);

    /* Key state */
    send_event(virtual_touchpad_fd, EV_KEY, BTN_TOUCH, 1);
    send_event(virtual_touchpad_fd, EV_KEY, BTN_TOOL_DOUBLETAP, 1);

    send_sync(virtual_touchpad_fd);
    is_touching = 1;
}

/* Move two fingers */
static void move_fingers(int delta_x)
{
    finger1_x += delta_x;
    finger2_x += delta_x;

    /* Clamp to valid range */
    if (finger1_x < 0) finger1_x = 0;
    if (finger1_x > TOUCHPAD_MAX_X) finger1_x = TOUCHPAD_MAX_X;
    if (finger2_x < 0) finger2_x = 0;
    if (finger2_x > TOUCHPAD_MAX_X) finger2_x = TOUCHPAD_MAX_X;

    /* Update finger 1 position */
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_SLOT, 0);
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_POSITION_X, finger1_x);

    /* Update finger 2 position */
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_SLOT, 1);
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_POSITION_X, finger2_x);

    send_sync(virtual_touchpad_fd);
}

/* End two-finger touch */
static void end_touch(void)
{
    /* Release finger 1 */
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_SLOT, 0);
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_TRACKING_ID, -1);

    /* Release finger 2 */
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_SLOT, 1);
    send_event(virtual_touchpad_fd, EV_ABS, ABS_MT_TRACKING_ID, -1);

    /* Key state */
    send_event(virtual_touchpad_fd, EV_KEY, BTN_TOUCH, 0);
    send_event(virtual_touchpad_fd, EV_KEY, BTN_TOOL_DOUBLETAP, 0);

    send_sync(virtual_touchpad_fd);
    is_touching = 0;
}

/* Timeout monitoring thread */
static void *timeout_monitor(void *arg)
{
    (void)arg;

    while (running) {
        usleep(50000); /* Check every 50ms */

        pthread_mutex_lock(&lock);
        if (pending_release && is_touching) {
            struct timeval now;
            gettimeofday(&now, NULL);

            long elapsed = (now.tv_sec - last_scroll_time.tv_sec) * 1000000 +
                          (now.tv_usec - last_scroll_time.tv_usec);

            if (elapsed >= idle_timeout_us) {
                end_touch();
                pending_release = 0;
            }
        }
        pthread_mutex_unlock(&lock);
    }

    return NULL;
}

/* Handle horizontal scroll event */
static void handle_hwheel(int value)
{
    int delta_x = value * scroll_to_pixel_ratio;

    pthread_mutex_lock(&lock);

    if (!is_touching) {
        start_touch();
    }

    move_fingers(delta_x);

    gettimeofday(&last_scroll_time, NULL);
    pending_release = 1;

    pthread_mutex_unlock(&lock);
}

/* Create virtual mouse device */
static int create_virtual_mouse(void)
{
    struct uinput_setup usetup;
    int fd;

    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open /dev/uinput");
        return -1;
    }

    /* Enable event types */
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    /* Mouse buttons */
    ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(fd, UI_SET_KEYBIT, BTN_SIDE);
    ioctl(fd, UI_SET_KEYBIT, BTN_EXTRA);

    /* Relative axes */
    ioctl(fd, UI_SET_RELBIT, REL_X);
    ioctl(fd, UI_SET_RELBIT, REL_Y);
    ioctl(fd, UI_SET_RELBIT, REL_WHEEL);
    ioctl(fd, UI_SET_RELBIT, REL_WHEEL_HI_RES);
    /* Note: Not enabling REL_HWHEEL and REL_HWHEEL_HI_RES */

    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    strcpy(usetup.name, "Virtual Mouse (HWheel Filtered)");

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    printf("Virtual mouse device created\n");
    return fd;
}

/* Create virtual touchpad device */
static int create_virtual_touchpad(void)
{
    struct uinput_setup usetup;
    struct uinput_abs_setup abs_setup;
    int fd;

    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open /dev/uinput");
        return -1;
    }

    /* Enable event types */
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    /* Keys */
    ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_FINGER);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_DOUBLETAP);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_TRIPLETAP);

    /* Absolute axes */
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);

    /* Set up ABS_MT_SLOT */
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_MT_SLOT;
    abs_setup.absinfo.minimum = 0;
    abs_setup.absinfo.maximum = 1;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);

    /* Set up ABS_MT_TRACKING_ID */
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_MT_TRACKING_ID;
    abs_setup.absinfo.minimum = 0;
    abs_setup.absinfo.maximum = TOUCHPAD_MAX_TRACKING_ID;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);

    /* Set up ABS_MT_POSITION_X */
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_MT_POSITION_X;
    abs_setup.absinfo.minimum = 0;
    abs_setup.absinfo.maximum = TOUCHPAD_MAX_X;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);

    /* Set up ABS_MT_POSITION_Y */
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ABS_MT_POSITION_Y;
    abs_setup.absinfo.minimum = 0;
    abs_setup.absinfo.maximum = TOUCHPAD_MAX_Y;
    ioctl(fd, UI_ABS_SETUP, &abs_setup);

    /* Set properties to indicate touchpad */
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_BUTTONPAD);

    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5679;
    strcpy(usetup.name, "Virtual Touchpad (HWheel Swipe)");

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    printf("Virtual touchpad device created\n");
    return fd;
}

/* Signal handler */
static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* Cleanup */
static void cleanup(void)
{
    running = 0;

    if (timeout_thread_running) {
        pthread_join(timeout_thread, NULL);
    }

    if (mouse_fd >= 0) {
        ioctl(mouse_fd, EVIOCGRAB, 0); /* Release grab */
        close(mouse_fd);
    }

    if (virtual_mouse_fd >= 0) {
        ioctl(virtual_mouse_fd, UI_DEV_DESTROY);
        close(virtual_mouse_fd);
    }

    if (virtual_touchpad_fd >= 0) {
        ioctl(virtual_touchpad_fd, UI_DEV_DESTROY);
        close(virtual_touchpad_fd);
    }

    printf("Cleanup completed\n");
}

int main(int argc, char *argv[])
{
    struct input_event ev;
    char device_name[256] = "Unknown";

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mouse_device>\n", argv[0]);
        fprintf(stderr, "Example: %s /dev/input/event7\n", argv[0]);
        return 1;
    }

    /* Load configuration */
    load_config();

    /* Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Open mouse device */
    mouse_fd = open(argv[1], O_RDONLY);
    if (mouse_fd < 0) {
        perror("Failed to open mouse device");
        return 1;
    }

    ioctl(mouse_fd, EVIOCGNAME(sizeof(device_name)), device_name);
    printf("Opened mouse device: %s (%s)\n", argv[1], device_name);

    /* Grab mouse device */
    if (ioctl(mouse_fd, EVIOCGRAB, 1) < 0) {
        perror("Failed to grab mouse device");
        close(mouse_fd);
        return 1;
    }
    printf("Mouse device grabbed\n");

    /* Create virtual devices */
    virtual_mouse_fd = create_virtual_mouse();
    if (virtual_mouse_fd < 0) {
        cleanup();
        return 1;
    }

    virtual_touchpad_fd = create_virtual_touchpad();
    if (virtual_touchpad_fd < 0) {
        cleanup();
        return 1;
    }

    /* Wait for devices to be ready */
    usleep(200000);

    /* Start timeout monitoring thread */
    if (pthread_create(&timeout_thread, NULL, timeout_monitor, NULL) == 0) {
        timeout_thread_running = 1;
    }

    printf("Listening for events... Press Ctrl+C to exit\n");

    /* Main event loop */
    while (running) {
        ssize_t n = read(mouse_fd, &ev, sizeof(ev));
        if (n != sizeof(ev)) {
            if (errno == EINTR) continue;
            break;
        }

        /* Handle horizontal scroll wheel events */
        if (ev.type == EV_REL &&
            (ev.code == REL_HWHEEL || ev.code == REL_HWHEEL_HI_RES)) {
            handle_hwheel(ev.value);
            continue; /* Don't forward */
        }

        /* Forward other events to virtual mouse */
        send_event(virtual_mouse_fd, ev.type, ev.code, ev.value);
    }

    cleanup();
    return 0;
}
