# Zaid-Ultra

All-in-one Geometry Dash / Geode suite focused on performance, input latency and gameplay responsiveness.

## Current target

- Geometry Dash: **2.2081**
- Geode: **5.10.1**
- First build target: **Android64**

## Ultra Low Latency backend

The first Zaid-Ultra module focuses on Android input-to-frame latency.

### Implemented

- Goodix high report-rate switch through the verified `switch_report_rate` sysfs node.
- MediaTek `speed_touch_enable` boost when exposed by the kernel.
- Gameplay-thread priority boost (`nice=-8` directly, ROOT `renice -10` fallback).
- Gameplay-thread timer slack reduced to 1 ns.
- Optional experimental EGL swap interval 0 / No-VSync mode.
- Geode diagnostic logging for touch report rate and boost state.
- All ROOT work runs asynchronously so level initialization is not blocked by `su`.

### duchamp validation

On the tested Xiaomi/POCO `duchamp` device with Goodix BERLIN 9916R:

- Normal report mode: **240 Hz**
- High report mode: **480 Hz**
- Measured moving-touch reports: approximately **474-476 Hz effective**

The mod deliberately does **not** disable thermal protection, force realtime/FIFO scheduling, modify CPU governors, or write unknown touch-driver nodes.

## Build

```sh
geode build -p android64
```

GitHub Actions also builds the Android64 `.geode` artifact automatically.
