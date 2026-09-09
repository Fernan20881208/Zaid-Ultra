# v0.2.0-beta.2

- Fixed the ZU console button on Android by attaching it to the aspect-ratio-safe
  PauseLayer/EndLevelLayer side menus when Node IDs is available.
- Replaced the old absolute-position fallback with an explicitly anchored menu.
- Added an always-available `Abrir consola ZU` action in the mod settings for
  compatibility with pause-menu replacement mods.

# v0.2.0-beta.1

- Added modular PlayLayer profiles with deterministic enter/exit restoration.
- Added Dynamic 120 Hz per-window request, real refresh monitoring and exact
  AOSP refresh-setting fallback restoration.
- Added exact heads-up notification snapshot/restore without changing DND or
  app permissions.
- Reworked Goodix and `speed_touch` writes behind a persistent crash-recovery
  snapshot.
- Added measured own-process memory trim; no process killing or cache-drop
  placebo commands.
- Added FMOD output, sample-rate, DSP-buffer and mixer CPU diagnostics plus
  opt-in pre-init buffer modes.
- Added CBF-compatible raw touch timestamp, jitter and physics-boundary metrics.
- Added Extreme Demon detection and manual per-level profiles.
- Added read-only thermal/frequency/FPS/frametime monitoring and optional HUD.
- Added a safe root console with predefined actions and no arbitrary shell.
- Added a read-only HyperOS `screenrecord` capability probe as the prerequisite
  for the true retroactive 60-second replay backend.

# v0.1.0

- Added Android ultra-low-latency backend.
- Added verified Goodix 240 Hz -> 480 Hz ROOT report-rate boost.
- Added MediaTek `speed_touch_enable` support.
- Added gameplay-thread priority tuning with ROOT fallback.
- Added 1 ns gameplay-thread timer slack option.
- Added optional experimental EGL No-VSync mode.
- Added touch-status diagnostic logs.
- Added Android64 GitHub Actions build.
