/*
 * Listen to mouse horizontal scroll wheel events, simulate touchpad two-finger
 * swipe in real-time
 * - Grab mouse device, block REL_HWHEEL/REL_HWHEEL_HI_RES
 * - Forward other mouse events to virtual mouse device
 * - Convert horizontal scroll events to two-finger swipe
 *
 * Environment variables:
 *   IDLE_TIMEOUT_MS - timeout to release touch (milliseconds), default 500
 *   SCROLL_RATIO - vertical scroll multiplier, default 1
 *   SCROLL_TO_PIXEL_RATIO - scroll value to pixel ratio, default -1
 *
 * Compile:
 *   gcc -o wheelswipe main.c
 * Run:
 *   sudo ./wheelswipe /dev/input/event7
 *   sudo SCROLL_TO_PIXEL_RATIO=-2 ./wheelswipe /dev/input/event7
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_X 1919
#define MAX_Y 1079
#define FINGER_SEP 200
#define TRACKING_ID_BASE 100
#define TOUCH_Y_BASE 500
#define TOUCH_Y_SPACING 100

#define DEFAULT_IDLE_TIMEOUT_MS 500
#define DEFAULT_SCROLL_RATIO 1
#define DEFAULT_SCROLL_TO_PIXEL_RATIO (-1)

static int idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS;
static int scroll_ratio = DEFAULT_SCROLL_RATIO;
static int scroll_to_pixel_ratio = DEFAULT_SCROLL_TO_PIXEL_RATIO;

static int mouse_fd = -1, v_mouse = -1, v_touch = -1;
static int is_touching = 0, finger_x = 960;
static volatile int running = 1;
static long long last_scroll_time = 0;

static long long current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void send_ev(int fd, int type, int code, int val) {
    if (fd < 0) return;

    struct input_event ev = { .type = type, .code = code, .value = val };
    if (write(fd, &ev, sizeof(ev)) < 0) {
        fprintf(stderr, "Error writing to fd %d (type %d, code %d): ", fd, type, code);
        perror("");

        if (errno == ENODEV || errno == EBADF) {
            running = 0;
        }
    }
}

static void syn(int fd) {
    send_ev(fd, EV_SYN, SYN_REPORT, 0);
}

static void lift_fingers(void) {
    if (!is_touching || v_touch < 0) return;
    for (int i = 0; i < 2; i++) {
        send_ev(v_touch, EV_ABS, ABS_MT_SLOT, i);
        send_ev(v_touch, EV_ABS, ABS_MT_TRACKING_ID, -1);
    }
    send_ev(v_touch, EV_KEY, BTN_TOUCH, 0);
    send_ev(v_touch, EV_KEY, BTN_TOOL_DOUBLETAP, 0);
    syn(v_touch);
    is_touching = 0;
    finger_x = 960;
}

// Robust cleanup: releases grab and closes all virtual devices
static void cleanup(void) {
    fprintf(stderr, "\nShutting down: releasing devices...\n");
    lift_fingers();
    if (mouse_fd >= 0) {
        ioctl(mouse_fd, EVIOCGRAB, 0);
        close(mouse_fd);
        mouse_fd = -1;
    }
    if (v_mouse >= 0) {
        ioctl(v_mouse, UI_DEV_DESTROY);
        close(v_mouse);
        v_mouse = -1;
    }
    if (v_touch >= 0) {
        ioctl(v_touch, UI_DEV_DESTROY);
        close(v_touch);
        v_touch = -1;
    }
}

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

static void load_config(void) {
    char* env;
    if ((env = getenv("IDLE_TIMEOUT_MS"))) {
        idle_timeout_ms = atoi(env);
        if (idle_timeout_ms <= 0) {
            fprintf(stderr, "Warning: IDLE_TIMEOUT_MS must be > 0, using default (%d)\n", DEFAULT_IDLE_TIMEOUT_MS);
            idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS;
        }
    }
    if ((env = getenv("SCROLL_RATIO"))) {
        scroll_ratio = atoi(env);
        if (scroll_ratio == 0) {
            fprintf(stderr, "Warning: SCROLL_RATIO must be != 0, using default (%d)\n", DEFAULT_SCROLL_RATIO);
            scroll_ratio = DEFAULT_SCROLL_RATIO;
        }
    }
    if ((env = getenv("SCROLL_TO_PIXEL_RATIO"))) {
        scroll_to_pixel_ratio = atoi(env);
        if (scroll_to_pixel_ratio == 0) {
            fprintf(stderr, "Warning: SCROLL_TO_PIXEL_RATIO must be != 0, using default (%d)\n", DEFAULT_SCROLL_TO_PIXEL_RATIO);
            scroll_to_pixel_ratio = DEFAULT_SCROLL_TO_PIXEL_RATIO;
        }
    }
}

#define IOCTL_OR_FAIL(fd, request, ...) \
    if (ioctl(fd, request, ##__VA_ARGS__) < 0) { perror(#request); close(fd); return -1; }

static int setup_dev(const char* name, int touch) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open /dev/uinput");
        return -1;
    }
    IOCTL_OR_FAIL(fd, UI_SET_EVBIT, EV_KEY);
    IOCTL_OR_FAIL(fd, UI_SET_EVBIT, EV_SYN);
    if (touch) {
        IOCTL_OR_FAIL(fd, UI_SET_EVBIT, EV_ABS);
        int keys[] = {BTN_TOUCH, BTN_TOOL_DOUBLETAP};
        for (int i = 0; i < 2; i++) IOCTL_OR_FAIL(fd, UI_SET_KEYBIT, keys[i]);
        int abs[] = {ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y};
        for (int i = 0; i < 4; i++) IOCTL_OR_FAIL(fd, UI_SET_ABSBIT, abs[i]);
        struct uinput_abs_setup s = {.code = ABS_MT_SLOT, .absinfo.maximum = 1};
        IOCTL_OR_FAIL(fd, UI_ABS_SETUP, &s);
        s.code = ABS_MT_TRACKING_ID;
        s.absinfo.maximum = 65535;
        IOCTL_OR_FAIL(fd, UI_ABS_SETUP, &s);
        s.code = ABS_MT_POSITION_X;
        s.absinfo.maximum = MAX_X;
        IOCTL_OR_FAIL(fd, UI_ABS_SETUP, &s);
        s.code = ABS_MT_POSITION_Y;
        s.absinfo.maximum = MAX_Y;
        IOCTL_OR_FAIL(fd, UI_ABS_SETUP, &s);
        IOCTL_OR_FAIL(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
    } else {
        IOCTL_OR_FAIL(fd, UI_SET_EVBIT, EV_REL);
        int rel[] = {REL_X, REL_Y, REL_WHEEL, REL_WHEEL_HI_RES};
        for (int i = 0; i < 4; i++) IOCTL_OR_FAIL(fd, UI_SET_RELBIT, rel[i]);
        int btn[] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA};
        for (int i = 0; i < 5; i++) IOCTL_OR_FAIL(fd, UI_SET_KEYBIT, btn[i]);
    }
    struct uinput_setup us = {.id.bustype = BUS_USB, .id.vendor = 0x1234, .id.product = touch ? 0x5679 : 0x5678};
    strncpy(us.name, name, UINPUT_MAX_NAME_SIZE - 1);
    us.name[UINPUT_MAX_NAME_SIZE - 1] = '\0';
    IOCTL_OR_FAIL(fd, UI_DEV_SETUP, &us);
    IOCTL_OR_FAIL(fd, UI_DEV_CREATE);
    return fd;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
        return 1;
    }
    load_config();

    // Use sigaction for more reliable signal catching
    struct sigaction sa = {.sa_handler = handle_signal};
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    atexit(cleanup);

    mouse_fd = open(argv[1], O_RDONLY);
    if (mouse_fd < 0 || ioctl(mouse_fd, EVIOCGRAB, 1) < 0) {
        perror("Failed to open or grab device");
        return 1;
    }

    v_mouse = setup_dev("V-Mouse", 0);
    v_touch = setup_dev("V-Touch", 1);
    if (v_mouse < 0 || v_touch < 0) {
        fprintf(stderr, "Failed to create virtual devices\n");
        return 1;
    }

    struct pollfd pfd = {.fd = mouse_fd, .events = POLLIN};
    struct input_event ev;

    while (running) {
        int timeout = -1; // Wait forever by default

        if (is_touching) {
            long long elapsed = current_time_ms() - last_scroll_time;
            if (elapsed >= idle_timeout_ms) {
                lift_fingers();
                timeout = -1; // Reset to wait forever now that fingers are up
            } else {
                timeout = idle_timeout_ms - (int)elapsed;
            }
        }

        int ret = poll(&pfd, 1, timeout);

        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll failed");
            break;
        }

        // If poll timed out (ret == 0) and we are touching, it's time to lift
        if (ret == 0 && is_touching) {
            lift_fingers();
            continue;
        }

        if (ret > 0 && (pfd.revents & (POLLERR | POLLHUP))) {
            fprintf(stderr, "Device disconnected\n");
            break;
        }

        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(mouse_fd, &ev, sizeof(ev));
            if (n != sizeof(ev)) {
                if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
                if (n < 0) perror("read failed");
                break;
            }

            if (ev.type == EV_REL) {
                if (ev.code == REL_HWHEEL_HI_RES) {
                    last_scroll_time = current_time_ms();
                    finger_x += (ev.value * scroll_to_pixel_ratio);
                    if (finger_x < 0) finger_x = 0;
                    if (finger_x > MAX_X - FINGER_SEP) finger_x = MAX_X - FINGER_SEP;

                    if (!is_touching) {
                        send_ev(v_touch, EV_KEY, BTN_TOUCH, 1);
                        send_ev(v_touch, EV_KEY, BTN_TOOL_DOUBLETAP, 1);
                        for (int i = 0; i < 2; i++) {
                            send_ev(v_touch, EV_ABS, ABS_MT_SLOT, i);
                            send_ev(v_touch, EV_ABS, ABS_MT_TRACKING_ID, TRACKING_ID_BASE + i);
                        }
                    }
                    for (int i = 0; i < 2; i++) {
                        send_ev(v_touch, EV_ABS, ABS_MT_SLOT, i);
                        send_ev(v_touch, EV_ABS, ABS_MT_POSITION_X, finger_x + (i == 1 ? FINGER_SEP : 0));
                        send_ev(v_touch, EV_ABS, ABS_MT_POSITION_Y, TOUCH_Y_BASE + (i * TOUCH_Y_SPACING));
                    }
                    syn(v_touch);
                    is_touching = 1;
                    continue;
                } else if (ev.code == REL_HWHEEL) {
                    continue;
                } else if (ev.code == REL_WHEEL || ev.code == REL_WHEEL_HI_RES) {
                    send_ev(v_mouse, ev.type, ev.code, ev.value * scroll_ratio);
                    continue;
                }
            }
            send_ev(v_mouse, ev.type, ev.code, ev.value);
        }
    }
    return 0;
}
