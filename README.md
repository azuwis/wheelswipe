# WheelSwipe

A Linux utility that converts horizontal mouse scroll wheel events to touchpad two-finger swipe gestures. This is particularly useful for the **Microsoft All-in-One Media Keyboard** and similar devices that have horizontal scroll wheels but lack native gesture support.

> **_NOTE:_** This program and README are mainly writen by LLM, use with caution.

## Problem

The Microsoft All-in-One Media Keyboard features a horizontal scroll wheel that generates `REL_HWHEEL` events. However, many desktop environments (like GNOME, KDE) interpret two-finger horizontal swipes on touchpads as workspace switching or other gestures, but don't recognize horizontal scroll wheel events for these actions.

This tool bridges that gap by:
1. Grabbing the mouse device to intercept horizontal scroll events
2. Converting `REL_HWHEEL` / `REL_HWHEEL_HI_RES` events to simulated touchpad two-finger swipes
3. Forwarding all other mouse events (movement, clicks, vertical scroll) to a virtual mouse device

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
# List input devices
cat /proc/bus/input/devices

# Or use evtest
sudo evtest
```

Look for your keyboard/mouse device. For Microsoft All-in-One Media Keyboard, it's typically listed as "Microsoft Microsoft All-in-One Media Keyboard".

### Run

```bash
sudo ./wheelswipe /dev/input/eventX
```

Replace `eventX` with your actual device (e.g., `event7`).

### With environment variables

```bash
sudo env IDLE_TIMEOUT_MS=300 SCROLL_TO_PIXEL_RATIO=-2 ./wheelswipe /dev/input/event7
```

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `IDLE_TIMEOUT_MS` | Time in milliseconds before releasing the simulated touch after scrolling stops (must be > 0) | `500` |
| `SCROLL_TO_PIXEL_RATIO` | Multiplier for converting scroll values to pixel movement. Negative values invert direction (must be != 0) | `-1` |
| `SCROLL_RATIO` | Multiplier for vertical scroll passthrough (must be != 0) | `1` |

## How It Works

1. **Device Grabbing**: The tool uses `EVIOCGRAB` to exclusively grab the input device, preventing the original horizontal scroll events from reaching the system.

2. **Event Forwarding**: A virtual mouse device is created using uinput. All non-horizontal-scroll events are forwarded to maintain normal mouse functionality.

3. **Gesture Simulation**: A virtual touchpad device is created. When horizontal scroll events are detected, the tool simulates two fingers touching the virtual touchpad and moving horizontally.

4. **Timeout Release**: After scrolling stops, the tool waits for the configured timeout before releasing the simulated touch, allowing for smooth gesture recognition.

## Supported Devices

- Microsoft All-in-One Media Keyboard (primary target)
- Any device that generates `REL_HWHEEL_HI_RES` events

## Troubleshooting

### Permission denied
```bash
# Make sure to run with sudo
sudo ./wheelswipe /dev/input/eventX
```

### Device not found
```bash
# List available input devices
ls -la /dev/input/

# Check device details
sudo evtest /dev/input/eventX
```

### Gestures not recognized
- Ensure your desktop environment has gesture support enabled
- Try adjusting `SCROLL_TO_PIXEL_RATIO` for sensitivity
- Increase `IDLE_TIMEOUT_MS` if gestures are being cut off

### Events not blocked
- Make sure no other program is grabbing the device
- Check that the correct event device is specified

## Running as a Service

Create a systemd service file `/etc/systemd/system/wheelswipe.service`:

```ini
[Unit]
Description=Wheelswipe - Horizontal scroll to gesture converter
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
sudo systemctl enable wheelswipe
sudo systemctl start wheelswipe
```

**Note**: The event device path may change on reboot. Consider using udev rules for persistent device naming.
