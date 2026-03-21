# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WheelSwipe is a Linux utility written in C that converts horizontal mouse scroll wheel events (REL_HWHEEL/REL_HWHEEL_HI_RES) to touchpad two-finger swipe gestures. This enables desktop environments to recognize horizontal scroll wheels as workspace-switching gestures.

The primary target device is the Microsoft All-in-One Media Keyboard, which has a horizontal scroll wheel but lacks native gesture support.

## Build Commands

```bash
# Build the binary
make

# Clean build artifacts
make clean

# Format code with astyle
make format
```

The project uses a simple Makefile that compiles main.c with gcc using `-Wall -Wextra`.

## Running the Program

```bash
# Basic usage (requires root for device access)
sudo ./wheelswipe /dev/input/eventX

# With environment variables
sudo env IDLE_TIMEOUT_MS=300 SCROLL_TO_PIXEL_RATIO=-2 ./wheelswipe /dev/input/event7

# Find device event number
cat /proc/bus/input/devices
# or
sudo evtest
```

## Architecture

### Single-File Design
The entire program is contained in `main.c` (approximately 290 lines). There are no header files or modules.

### Event Processing Flow

1. **Device Grabbing**: Uses EVIOCGRAB ioctl to exclusively grab the input device, preventing horizontal scroll events from reaching the system normally
2. **Virtual Device Creation**: Creates two virtual uinput devices:
   - V-Mouse: Forwards all non-horizontal-scroll events (movement, clicks, vertical scroll) to maintain normal mouse functionality
   - V-Touch: Simulates a touchpad with two-finger multitouch capability (Protocol B)
3. **Event Translation**:
   - REL_HWHEEL_HI_RES events → two-finger horizontal swipe on virtual touchpad
   - REL_HWHEEL events → ignored (to prevent duplicates)
   - All other events → forwarded to V-Mouse
4. **Timeout Management**: Uses poll() with dynamic timeout to release simulated touch after idle period

### Key Technical Details

- **Multitouch Protocol**: Uses MT Protocol B (slot-based) with ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X/Y
- **Virtual Touchpad Dimensions**: 1920x1080 (MAX_X=1919, MAX_Y=1079)
- **Finger Separation**: 200 pixels (FINGER_SEP constant); finger_x is clamped to [0, MAX_X - FINGER_SEP] so finger 1 never exceeds MAX_X
- **Touch Keys**: Registers BTN_TOUCH, BTN_TOOL_FINGER, and BTN_TOOL_DOUBLETAP. Touch-down sends BTN_TOUCH=1 and BTN_TOOL_DOUBLETAP=1; BTN_TOOL_FINGER is intentionally not set to 1 (correct MT Protocol B behavior: BTN_TOOL_FINGER=1 is for single-finger contact, BTN_TOOL_DOUBLETAP=1 is for two-finger). Lift clears all three to 0. BTN_TOOL_FINGER must remain registered (via UI_SET_KEYBIT) and cleared on lift — removing it causes applications (e.g. Firefox) to misinterpret two-finger swipes as zoom-out gestures, because libinput uses BTN_TOOL_FINGER device capability to identify the device as a proper touchpad
- **Polling Strategy**: Dynamic timeout - infinite when idle, calculated remaining time when touching
- **Error Handling**: Checks for ENODEV/EBADF on write failures to gracefully exit on device removal

### Environment Variables

| Variable | Default | Purpose |
|----------|---------|---------|
| IDLE_TIMEOUT_MS | 500 | Time to wait after last scroll before releasing touch (must be > 0) |
| SCROLL_TO_PIXEL_RATIO | -1 | Multiplier for scroll→pixel conversion (must be != 0; negative inverts direction) |
| SCROLL_RATIO | 1 | Multiplier for vertical scroll passthrough (must be != 0) |

### State Management

Global state variables:
- `is_touching`: Whether virtual touch is currently active
- `finger_x`: Current X position of the simulated fingers
- `last_scroll_time`: Timestamp of last scroll event (for timeout calculation)
- `running`: Signal handler flag for graceful shutdown

### Cleanup and Signal Handling

- Uses sigaction for SIGTERM/SIGINT handling (more reliable than signal())
- atexit() registered cleanup function that:
  - Lifts virtual fingers (releases touch)
  - Releases device grab (EVIOCGRAB 0)
  - Closes all file descriptors

## Development Environment

This project uses Nix for development environment:

```bash
# Enter development shell (if using direnv)
direnv allow

# Or manually
nix-shell
```

The shell.nix provides:
- astyle (code formatter)
- claude-sandboxed (sandboxed Claude Code binary with restricted closure)

Dependencies are managed via sources.nix with a custom update mechanism for nixpkgs and agent-sandbox.nix.

## Code Style

- Uses astyle for formatting
- 4-space indentation
- K&R-style bracing for functions
- Compact, inline style for simple operations
