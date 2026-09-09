# Android backend research and safety boundaries

This document records why each Android action exists and what Zaid-Ultra will
not do. The target is Geometry Dash 2.2081, Geode 5.10.1 and Android64.

## Reversible state model

Persistent/global values are never written before their previous state is
captured and saved to `root-state-v1.txt` in the mod save directory. The ROOT
worker is serialized, so an exit restore cannot overtake an enter operation.
If the process dies during gameplay, the next launch restores the saved values
before accepting a new profile.

| Feature | Interface | Restore rule |
| --- | --- | --- |
| Window refresh | `WindowManager.LayoutParams.preferredDisplayModeId` and `preferredRefreshRate` | Restore both prior fields before `PlayLayer::onExit` completes |
| ROOT refresh fallback | AOSP settings `system min_refresh_rate` and `peak_refresh_rate` | Put the exact old value, or delete the key if it was absent |
| Heads-up guard | AOSP setting `global heads_up_notifications_enabled` | Put/delete its exact old state; never alter DND or per-app permissions |
| Goodix boost | `/sys/devices/platform/goodix_ts.0/switch_report_rate` | Only values `0`/`1`; exact old value restored |
| MediaTek touch boost | `/sys/module/metis/parameters/speed_touch_enable` | Only boolean switch values; exact old value restored |
| Thread scheduling | `PR_SET_TIMERSLACK`, `setpriority` / `renice` | Restore captured timer slack and nice value; never use realtime policy |

The two touch nodes are OEM interfaces without a stable public ABI. They are
allow-listed solely because their semantics were measured on the target
duchamp/Goodix BERLIN 9916R device. No wildcard discovery or speculative sysfs
writes are permitted.

## Refresh rate

Android's public per-window request is preferred. The selected display mode is
restricted to the current physical resolution and must be within 0.6 Hz of
120 Hz. `Display.getRefreshRate()` is sampled during gameplay; transitions to
60/90 Hz are logged rather than hidden.

The optional ROOT fallback uses the same min/peak settings consumed by AOSP's
display mode policy. It does not touch vendor display nodes. References:

- [WindowManager.LayoutParams preferredDisplayModeId](https://developer.android.com/reference/android/view/WindowManager.LayoutParams#preferredDisplayModeId)
- [WindowManager.LayoutParams preferredRefreshRate](https://developer.android.com/reference/android/view/WindowManager.LayoutParams#preferredRefreshRate)
- [Display.getRefreshRate](https://developer.android.com/reference/android/view/Display#getRefreshRate())

## Memory

The only trim is `malloc_trim(0)` resolved inside the current process. Zaid-Ultra
measures `/proc/self/status` `VmRSS` before and after. If an automatic run does
not reclaim at least 1 MiB, automatic repeats are disabled for that app session.
It never kills apps, calls `drop_caches`, or invokes Android task killers.

## Notifications and overlays

Only the global heads-up switch is currently implemented. DND/Modes are not
used because Android version and OEM policy can transform their state, making
exact restoration unreliable. `SYSTEM_ALERT_WINDOW` / app-op changes are also
excluded until a scoped, reversible method is validated. Android 12's public
`Window.setHideOverlayWindows` is not called: it requires the privileged
`HIDE_OVERLAY_WINDOWS` permission, and `Window` exposes no matching public
getter with which a mod could snapshot the prior state exactly.

- [AOSP Window.setHideOverlayWindows implementation](https://android.googlesource.com/platform/frameworks/base/+/master/core/java/android/view/Window.java)

## Audio

Geometry Dash uses FMOD Core. `System::setDSPBufferSize` is valid only before
`System::init`, so audio tuning is a restart-required setting rather than a
per-level toggle. `Original` is the default. `Auto seguro` only changes the DSP
block when Android publishes `PROPERTY_OUTPUT_FRAMES_PER_BUFFER`; the request is
clamped to a 128–512 frame power of two and keeps four buffers.

Diagnostics report FMOD output (including AAudio/OpenSL ES), driver, software
sample rate, DSP block/count, nominal mixer queue and FMOD DSP/stream CPU. FMOD
Core exposes no mixer-underrun counter through this API, so the UI says so
instead of inventing a metric.

- [AudioManager output properties](https://developer.android.com/reference/android/media/AudioManager#PROPERTY_OUTPUT_FRAMES_PER_BUFFER)
- [FMOD System setDSPBufferSize](https://www.fmod.com/docs/2.03/api/core-api-system.html#system_setdspbuffersize)

## Input and CBF

Geode 5.10.1 forwards Android MotionEvent nanosecond timestamps through
`AndroidRichInputEvent` and Geometry Dash's queued `PlayerButtonCommand`.
Click Between Frames consumes that timestamp to split a physics step. The
Zaid-Ultra listener always returns `Propagate`; its physics hook only marks an
observed boundary and never changes delta time, queued buttons or player state.
The displayed CBF phase mirrors its timestamp calculation against observed
frame spans split into approximately 240 Hz steps. It is labelled an estimate
because independent hook ordering can add a small constant offset.

`touch app Hz` is the delivered MotionEvent rate, which may be lower than the
Goodix SYN_REPORT rate because Android may batch/coalesce motion. It is not
labelled as the hardware rate.

## Thermal monitoring

Thermal zones, cpufreq policies, GPU devfreq, battery temperature and Linux
`thermal_pressure` are read only. Positive thermal pressure is direct evidence
of capacity reduction; the high-temperature/low-frequency indicator is clearly
labelled heuristic. Zaid-Ultra never disables thermal protection.

## Instant Replay

The target HyperOS `screenrecord` v1.4 was tested under KernelSU and accepted
the undocumented AOSP raw-stream form `--output-format=h264 ... -`. A two-second
probe exited successfully, emitted 1,047,920 bytes and began with valid Annex B
SPS/PPS NAL units. The same device exposes `app_process64`, Remote Submix input
and output ports, and MIUIScreenRecorder.

The first live backend therefore reads only the already-encoded AVC stream on a
dedicated native thread. It retains at most 72 seconds or 96 MiB and snapshots
from a keyframe at or after the 60-second cutoff. Save uses Android's NDK
MediaMuxer dynamically, so no media symbols are resolved while Geode loads; a
raw AVC file is preserved if OEM muxing fails. Capture is opt-in and exists only
for the PlayLayer lifetime. The screenrecord PID is written to one fixed file
and is signalled only after `/proc/<pid>/comm` verifies the exact process name;
there is no broad `pkill`.

Raw `screenrecord` contains no audio, so audio is kept in a separate reversible
module. A small dex helper runs only for the PlayLayer lifetime through ROOT
`app_process`. On Android 13+ it registers an AudioPolicy mix matching the
three playback-capture usages allowed by Android (`USAGE_UNKNOWN`,
`USAGE_MEDIA`, and `USAGE_GAME`) and the exact Geometry Dash/Geode Linux UID.
The mix uses `ROUTE_FLAG_LOOP_BACK_RENDER`, which loops playback into an
AudioRecord while retaining the normal device output. The UID predicate avoids
recording unrelated apps or notification audio. PCM is measured before the
encoder so the console can distinguish a real signal from valid AAC packets
that contain silence, then hardware-encoded to AAC-LC at 48 kHz stereo /
192 kbps. Each packet carries a monotonic AudioRecord/MediaCodec PTS over a
framed pipe; the native ring retains at most 72 seconds or 4 MiB and MediaMuxer
interleaves it with AVC relative to the selected video keyframe.

MediaProjection playback capture was not selected for this in-mod backend: it
requires a user-granted projection token, `RECORD_AUDIO`, and on current Android
targets a declared foreground service of type `mediaProjection`. Those manifest
requirements belong to an APK and cannot be added safely by a `.geode` resource
at runtime. The scoped ROOT AudioPolicy route changes no persistent permission
or audio setting and disappears with the helper process. If the OEM path is
incompatible, the helper exits, its error/signal state is shown in the console,
and replay continues safely as video-only.

References:

- [Android AudioPlaybackCaptureConfiguration](https://developer.android.com/reference/android/media/AudioPlaybackCaptureConfiguration)
- [Android MediaProjection foreground-service requirements](https://developer.android.com/media/grow/media-projection)
- [AOSP AudioMix `ROUTE_FLAG_LOOP_BACK_RENDER`](https://cs.android.com/android/platform/superproject/main/+/main:frameworks/base/media/java/android/media/audiopolicy/AudioMix.java)
