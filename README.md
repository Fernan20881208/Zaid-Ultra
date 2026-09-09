# Zaid-Ultra

All-in-one Geometry Dash / Geode suite focused on performance, input latency
and gameplay responsiveness.

## Current target

- Geometry Dash: **2.2081**
- Geode: **5.10.1**
- First build target: **Android64**

## v0.2 modular backend

Zaid-Ultra applies a gameplay profile only while `PlayLayer` exists and keeps
persistent ROOT writes behind one serialized, crash-recoverable state guard.

### Implemented in the v0.2 beta

- Dynamic 120 Hz request through the Android window plus an optional AOSP
  `min_refresh_rate` / `peak_refresh_rate` ROOT fallback.
- Exact snapshots and restoration for refresh settings, notification heads-up,
  Goodix report mode and `speed_touch_enable`, including next-launch recovery.
- Own-process `malloc_trim` with RSS/timing measurement and automatic disabling
  when it provides no measurable benefit.
- FMOD/Android audio-route diagnostics and opt-in pre-init DSP buffer modes.
- Raw Android MotionEvent timestamp metrics that always propagate to CBF:
  delivery rate, jitter, dispatch age and time to an observed physics boundary.
- Automatic Extreme Demon profile plus manual `ID:ultra` / `ID:monitor` rules.
- Read-only CPU/GPU/battery thermal and frequency monitoring, thermal-pressure
  detection, FPS, frametime, display refresh and optional in-level overlay.
- Safe Geode console on pause/end screens with fixed diagnostic actions only,
  plus an alternate opener in the mod settings for replaced pause menus.
- Read-only `screenrecord --help` capability probe for the 60-second replay
  backend. Recording remains disabled until the actual HyperOS flags and audio
  capabilities are validated on-device.

### duchamp validation

On the tested Xiaomi/POCO `duchamp` device with Goodix BERLIN 9916R:

- Normal report mode: **240 Hz**
- High report mode: **480 Hz**
- Measured moving-touch reports: approximately **474-476 Hz effective**

The mod deliberately does **not** disable thermal protection, force
realtime/FIFO scheduling, kill other processes, modify CPU/GPU governors,
overclock/undervolt, or write unknown touch-driver nodes.

Detailed semantics and safety boundaries are recorded in
[`docs/ANDROID_RESEARCH.md`](docs/ANDROID_RESEARCH.md).

## Build

```sh
geode build -p android64
```

GitHub Actions also builds the Android64 `.geode` artifact automatically.
