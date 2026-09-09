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
- Android audio-route diagnostics. Direct FMOD tuning is temporarily disabled
  in beta.3 while its Android load-time ABI path is validated.
- Raw Android MotionEvent timestamp metrics that always propagate to CBF:
  delivery rate, jitter, dispatch age and time to an observed physics boundary.
- Automatic Extreme Demon profile plus manual `ID:ultra` / `ID:monitor` rules.
- Read-only CPU/GPU/battery thermal and frequency monitoring, thermal-pressure
  detection, FPS, frametime, display refresh and optional in-level overlay.
- Safe Geode console on pause/end screens with fixed diagnostic actions only.
- Opt-in ROOT Instant Replay beta using the device-validated raw-H.264
  `screenrecord` stream: a bounded encoded ring keeps recent gameplay and the
  pause/end Clip button saves existing frames without restarting capture.
  Android MediaMuxer produces MP4 when accepted, with a raw-H.264 diagnostic
  fallback. Internal audio remains disabled until the verified `REMOTE_SUBMIX`
  route has exact packet timestamps.

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
