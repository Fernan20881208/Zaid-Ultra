# v0.2.0-beta.10

- Añade el nuevo logo Zaid como icono oficial del mod y como botón flotante
  ligero del controlador de replay.
- Convierte los dos nodos ya verificados del dispositivo en un modo táctil
  global forzoso: escribe exactamente `1` en Goodix y `speed_touch_enable`,
  comprueba la lectura y vuelve a aplicarlos si cambian durante el juego.
- Mantiene 480 Hz entre niveles, sin depender de un perfil ni de una opción
  desactivable. El valor original queda guardado antes de escribir y se
  restaura al cerrar normalmente, con recuperación en el siguiente arranque
  si el proceso termina de forma inesperada.
- La consola segura muestra el resultado verificado y su botón Restaurar puede
  devolver el estado previo hasta que se reinicie el juego.

# v0.2.0-beta.9

- Añade un botón ZU flotante y arrastrable dentro de PlayLayer. Un toque abre
  controles manuales para Grabar, Pausar/Seguir, Finalizar y guardar Clip 60s.
- El inicio automático del replay ahora es opcional y queda apagado por
  defecto, de modo que el usuario decide cuándo existe carga de captura.
- Pausar conserva los anillos H.264 y AAC; al reanudar se compacta el intervalo
  pausado manteniendo una línea de tiempo compartida para vídeo y audio.
- Finalizar detiene ambos procesos ROOT y guarda automáticamente el tramo
  disponible (máximo 60 segundos). Guardar Clip sigue siendo retroactivo y no
  detiene la captura activa.
- El control solo reclama toques sobre el propio botón, por lo que no consume
  la entrada de gameplay ni altera CBF fuera de esa zona.

# v0.2.0-beta.8

- Corrige el replay AAC silencioso: el backend ahora incluye `USAGE_GAME` y `USAGE_UNKNOWN`, además de `USAGE_MEDIA`, y restringe la mezcla al UID real de Geometry Dash/Geode.
- Añade medición PCM previa al encoder. La consola distingue una señal real de paquetes AAC que contienen silencio y muestra el nivel en dBFS.
- Conserva el loopback con render al altavoz/audífonos; no instala servicios ni modifica permisos persistentes.

# v0.2.0-beta.7

- Fixed Goodix BERLIN report-rate detection: the verified human-readable
  `touch report rate::240HZ/480HZ` readback is now mapped to the node's exact
  `0/1` write semantics. The 480 Hz request is read back and verified.
- Added an isolated ROOT `app_process` audio helper for Instant Replay. It uses
  a temporary `USAGE_MEDIA` AudioPolicy `LOOP_BACK_RENDER` mix, keeps local
  playback enabled, hardware-encodes AAC-LC at 48 kHz stereo / 192 kbps, and
  sends timestamped packets to the bounded replay ring.
- MP4 saving now interleaves timestamped AVC and AAC packets. If the ROM rejects
  the audio route, video replay remains available and the safe console displays
  the helper's error instead of failing the whole capture.

# v0.2.0-beta.6

- Fixed Save Clip rejecting a valid encoded replay after PlayLayer exited.
  The frozen SPS/PPS and frame ring can now be saved from the end-level menu.
- Changed the normal post-gameplay replay state from `buffer stopped` to
  `buffer ready` so it is not confused with a capture failure.

# v0.2.0-beta.5

- Added the first live 60-second ROOT Instant Replay backend for the validated
  HyperOS raw-H.264 `screenrecord` stream.
- Capture runs continuously only while PlayLayer is active and only when the
  opt-in setting is enabled; Save Clip snapshots existing encoded frames and
  never starts a new recording.
- Added a bounded 72-second / 96 MiB encoded ring, AVC keyframe-aware trimming,
  background MP4 remuxing through dynamically loaded Android NDK MediaMuxer,
  and a raw-H.264 fallback if the device muxer rejects the stream.
- Added visible Clip buttons to pause and end-level menus plus replay state,
  duration, memory use and output path in the safe ZU console.
- Audio is deliberately not claimed in this build. The verified
  `REMOTE_SUBMIX` route will be integrated only through a timestamped helper so
  it cannot desynchronize or destabilize Geometry Dash.

# v0.2.0-beta.4

- Fixed the Android 16 crash when entering a level under Geode Launcher.
- Replaced the nonexistent `Cocos2dxActivity.getContext()` JNI call with the
  launcher's preserved `BaseRobTopActivity` activity reference.
- Added strict exception cleanup to both the current-launcher and legacy JNI
  lookup paths so a failed optional lookup cannot poison the next mod's JNI
  call and trigger an ART abort.

# v0.2.0-beta.3

- Hardened Android startup after an on-device crash while Geode was loading
  the Zaid-Ultra binary.
- Deferred ROOT worker startup, crash recovery, JNI audio probing and raw-input
  listener registration until the first PlayLayer.
- Removed the direct `FMOD::System::init` hook and all direct FMOD C++ symbol
  references from this safe build; Android audio properties remain read-only.
- Temporarily removed the settings-page console action to keep the mod-loading
  path free of runtime event listeners. The pause/end-level ZU button remains.

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
