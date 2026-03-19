# WheelSwipe

A Linux utility that converts horizontal mouse scroll wheel events to touchpad two-finger swipe gestures, enabling applications like Firefox and Chromium to recognise them as back/forward navigation, and desktop environments to use them for workspace switching.

> **Note:** This project is mainly written by LLM, use with caution.

## Problem

Applications like Firefox and Chromium interpret two-finger horizontal swipes on touchpads as back/forward navigation. Desktop environments like GNOME and KDE use them for workspace switching. However, none of these recognise horizontal scroll wheel events from mice. The Microsoft All-in-One Media Keyboard has a horizontal scroll wheel that generates these events, so WheelSwipe bridges the gap.

## How It Works

1. Exclusively grabs the input device via `EVIOCGRAB`, blocking its events from the rest of the system
2. Creates a virtual mouse (V-Mouse) and forwards all non-horizontal-scroll events to it
3. Creates a virtual touchpad (V-Touch) and converts `REL_HWHEEL_HI_RES` events to two-finger horizontal swipes using MT Protocol B; `REL_HWHEEL` events are ignored to prevent duplicates

## Requirements

- Linux with uinput support
- Root privileges (for device access)
- GCC

## Build

```bash
make
```

## Usage

### Find your device

```bash
cat /proc/bus/input/devices
# or
sudo evtest
```

### Run

```bash
sudo ./wheelswipe /dev/input/eventX
```

### With environment variables

```bash
sudo env IDLE_TIMEOUT_MS=300 SCROLL_TO_PIXEL_RATIO=-2 ./wheelswipe /dev/input/event7
```

## Environment Variables

| Variable | Default | Constraint | Description |
|----------|---------|------------|-------------|
| `IDLE_TIMEOUT_MS` | `500` | > 0 | Milliseconds to wait after last scroll before releasing the simulated touch |
| `SCROLL_TO_PIXEL_RATIO` | `-1` | != 0 | Scroll-to-pixel multiplier; negative inverts swipe direction |
| `SCROLL_RATIO` | `1` | != 0 | Vertical scroll passthrough multiplier |

Invalid values produce a warning and fall back to the default.

## Running as a Service

Create `/etc/systemd/system/wheelswipe.service`:

```ini
[Unit]
Description=WheelSwipe - horizontal scroll to gesture converter
After=multi-user.target

[Service]
Type=simple
ExecStart=/usr/local/bin/wheelswipe /dev/input/eventX
Restart=on-failure
RestartSec=5
Environment="IDLE_TIMEOUT_MS=500"
Environment="SCROLL_TO_PIXEL_RATIO=-1"

[Install]
WantedBy=multi-user.target
```

Then:

```bash
sudo cp wheelswipe /usr/local/bin/
sudo systemctl daemon-reload
sudo systemctl enable --now wheelswipe
```

> **Note:** The event device path may change on reboot. Consider using udev rules for a stable path.

## Troubleshooting

**Permission denied** — run with `sudo`.

**Wrong device** — use `sudo evtest` to confirm the device generates `REL_HWHEEL_HI_RES` events.

**Gestures not recognised** — ensure gesture support is enabled in your desktop environment or browser. Try adjusting `SCROLL_TO_PIXEL_RATIO` for sensitivity or increasing `IDLE_TIMEOUT_MS` if gestures are cut short.

**Events not blocked** — ensure no other program is grabbing the device.
