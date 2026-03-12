/*
 * Listen to mouse horizontal scroll wheel events, simulate touchpad two-finger
 * swipe in real-time
 * - Grab mouse device, block REL_HWHEEL/REL_HWHEEL_HI_RES
 * - Forward other mouse events to virtual mouse device
 * - Convert horizontal scroll events to two-finger swipe
 *
 * Environment variables:
 *   IDLE_TIMEOUT_MS - timeout to release touch (milliseconds), default 500
 *   SCROLL_TO_PIXEL_RATIO - scroll value to pixel ratio, default -1
 *
 * Compile:
 *   gcc -o wheelswipe main.c
 * Run:
 *   sudo ./wheelswipe /dev/input/event7
 *   sudo SCROLL_TO_PIXEL_RATIO=-2 ./wheelswipe /dev/input/event7
 */

#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_X 1919
#define MAX_Y 1079
#define FINGER_SEP 200

/* Original Defaults */
#define DEFAULT_IDLE_TIMEOUT_MS 500
#define DEFAULT_SCROLL_RATIO 1
#define DEFAULT_SCROLL_TO_PIXEL_RATIO (-1)

static int idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS;
static int scroll_ratio = DEFAULT_SCROLL_RATIO;
static int scroll_to_pixel_ratio = DEFAULT_SCROLL_TO_PIXEL_RATIO;

static int mouse_fd, v_mouse, v_touch;
static int is_touching = 0, finger_x = 960;
static volatile int running = 1;

void load_config(void) {
    char* env;
    if ((env = getenv("IDLE_TIMEOUT_MS"))) idle_timeout_ms = atoi(env);
    if ((env = getenv("SCROLL_RATIO"))) scroll_ratio = atoi(env);
    if ((env = getenv("SCROLL_TO_PIXEL_RATIO")))
        scroll_to_pixel_ratio = atoi(env);

    printf("Config: Timeout=%dms, ScrollRatio=%d, Sensitivity=%d\n",
           idle_timeout_ms, scroll_ratio, scroll_to_pixel_ratio);
}

void send_ev(int fd, int type, int code, int val) {
    struct input_event ev = {.type = type, .code = code, .value = val};
    gettimeofday(&ev.time, NULL);
    if (write(fd, &ev, sizeof(ev)) < 0) {
    }
}

void syn(int fd) {
    send_ev(fd, EV_SYN, SYN_REPORT, 0);
}

void lift_fingers() {
    if (!is_touching) return;
    for (int i = 0; i < 2; i++) {
        send_ev(v_touch, EV_ABS, ABS_MT_SLOT, i);
        send_ev(v_touch, EV_ABS, ABS_MT_TRACKING_ID, -1);
    }
    send_ev(v_touch, EV_KEY, BTN_TOUCH, 0);
    send_ev(v_touch, EV_KEY, BTN_TOOL_FINGER, 0);
    send_ev(v_touch, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
    syn(v_touch);
    is_touching = 0;
    finger_x = 960;
}

void move_fingers(int delta) {
    finger_x += (delta * scroll_to_pixel_ratio);
    if (finger_x < 0) finger_x = 0;
    if (finger_x > MAX_X) finger_x = MAX_X;

    for (int i = 0; i < 2; i++) {
        send_ev(v_touch, EV_ABS, ABS_MT_SLOT, i);
        if (!is_touching) send_ev(v_touch, EV_ABS, ABS_MT_TRACKING_ID, 10 + i);
        send_ev(v_touch, EV_ABS, ABS_MT_POSITION_X,
                finger_x + (i == 1 ? FINGER_SEP : 0));
        send_ev(v_touch, EV_ABS, ABS_MT_POSITION_Y, 500 + (i * 100));
    }
    if (!is_touching) {
        send_ev(v_touch, EV_KEY, BTN_TOUCH, 1);
        send_ev(v_touch, EV_KEY, BTN_TOOL_FINGER, 0);
        send_ev(v_touch, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
    }
    syn(v_touch);
    is_touching = 1;
}

int setup_dev(const char* name, int touch) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    if (touch) {
        ioctl(fd, UI_SET_EVBIT, EV_ABS);
        int keys[] = {BTN_TOUCH, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP, BTN_LEFT};
        for (int i = 0; i < 4; i++) ioctl(fd, UI_SET_KEYBIT, keys[i]);
        int abs[] = {ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X,
                     ABS_MT_POSITION_Y
                    };
        for (int i = 0; i < 4; i++) ioctl(fd, UI_SET_ABSBIT, abs[i]);

        struct uinput_abs_setup s = {.code = ABS_MT_SLOT, .absinfo.maximum = 1};
        ioctl(fd, UI_ABS_SETUP, &s);
        s.code = ABS_MT_TRACKING_ID;
        s.absinfo.maximum = 65535;
        ioctl(fd, UI_ABS_SETUP, &s);
        s.code = ABS_MT_POSITION_X;
        s.absinfo.maximum = MAX_X;
        ioctl(fd, UI_ABS_SETUP, &s);
        s.code = ABS_MT_POSITION_Y;
        s.absinfo.maximum = MAX_Y;
        ioctl(fd, UI_ABS_SETUP, &s);

        ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
        ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_BUTTONPAD);
    } else {
        ioctl(fd, UI_SET_EVBIT, EV_REL);
        int rel[] = {REL_X, REL_Y, REL_WHEEL, REL_WHEEL_HI_RES};
        for (int i = 0; i < 4; i++) ioctl(fd, UI_SET_RELBIT, rel[i]);
        int btn[] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA};
        for (int i = 0; i < 5; i++) ioctl(fd, UI_SET_KEYBIT, btn[i]);
    }
    struct uinput_setup us = {.id.bustype = BUS_USB,
                                  .id.vendor = 0x1234,
                                  .id.product = touch ? 0x5679 : 0x5678
    };
    strncpy(us.name, name, UINPUT_MAX_NAME_SIZE);
    ioctl(fd, UI_DEV_SETUP, &us);
    ioctl(fd, UI_DEV_CREATE);
    return fd;
}

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    load_config();
    signal(SIGTERM, (void*)exit);
    signal(SIGINT, (void*)exit);

    mouse_fd = open(argv[1], O_RDONLY);
    if (mouse_fd < 0 || ioctl(mouse_fd, EVIOCGRAB, 1) < 0) return 1;
    v_mouse = setup_dev("V-Mouse", 0);
    v_touch = setup_dev("V-Touch", 1);

    struct pollfd pfd = {.fd = mouse_fd, .events = POLLIN};
    struct input_event ev;
    while (running) {
        int ret = poll(&pfd, 1, is_touching ? idle_timeout_ms : 500);
        if (ret == 0) {
            lift_fingers();
            continue;
        }
        if (ret < 0) break;
        if (read(mouse_fd, &ev, sizeof(ev)) != sizeof(ev)) break;

        if (ev.type == EV_REL) {
            if (ev.code == REL_HWHEEL || ev.code == REL_HWHEEL_HI_RES) {
                move_fingers(ev.value);
                continue;
            } else if (ev.code == REL_WHEEL || ev.code == REL_WHEEL_HI_RES) {
                send_ev(v_mouse, ev.type, ev.code, ev.value * scroll_ratio);
                syn(v_mouse);
                continue;
            }
        }
        send_ev(v_mouse, ev.type, ev.code, ev.value);
        if (ev.type == EV_SYN) syn(v_mouse);
    }
    return 0;
}
