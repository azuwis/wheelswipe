# AGENTS.md

WheelSwipe converts horizontal mouse scroll wheel events into touchpad
two-finger swipe gestures. Usage and install instructions live in README.md.

## Constraints

- Never remove BTN_TOOL_FINGER registration or its lift-to-0 write.
  libinput identifies the device as a proper touchpad by this capability,
  and without it applications (e.g. Firefox) read two-finger swipes as
  zoom-out gestures.
- Every touch-down needs fresh ABS_MT_TRACKING_ID values. Keep the ID pair
  inside [TRACKING_ID_BASE, TRACKING_ID_MAX]. Receivers must not mistake a
  new touch for a continuation.
- `finger_x` must stay clamped to [0, MAX_X - FINGER_SEP] so the second
  finger stays on the virtual touchpad.
- Keep the poll timeout at -1 while idle. It counts down only while touching,
  so idle wake-ups stay rare.
- On write failure, exit cleanly for ENODEV or EBADF. The device is gone, so
  retrying is pointless.
- New environment variables need entries in the README table and in the
  comment block at the top of main.c.
