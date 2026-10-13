# GFN4K LSPosed ATV Spoof 0.2.1

Experimental per-process SHIELD identity test for rooted Android. Scope LSPosed **only** to `com.nvidia.tegrazone3`. The APK is not an officially supported NVIDIA device configuration and does **not** guarantee streaming at any resolution.

## 0.2.1 change

The previous 0.2.0 forcibly replaced `Build.HARDWARE` with `mdarcy` (a SHIELD device codename). Version 0.2.1 keeps the **real Qualcomm** `Build.HARDWARE` and preserves the TV UI/SHIELD feature spoofs. This isolates one possible incompatibility during native stream initialization, but is **not yet a verified fix**.

## Build

GitHub Actions > **Build GFN4K LSPosed APK** > download the `GFN4K-ATV-LSPosed-0.2.1` artifact after a successful run. Built using Gradle 8.10.2, Java 17, Android SDK 35, Xposed compileOnly API 82.

To update from 0.2.0 install the 0.2.1 APK, ensure it is enabled for GeForce NOW SHIELD only in LSPosed, and **force-stop** `com.nvidia.tegrazone3` to restart its process. The existing KernelSU SHIELD feature module can remain installed for this test.

## Recommended diagnostic order

1. Try SHIELD at 1080p60 with HDR off **over Wi-Fi**, with VPN disabled, and compare against the normal Android GeForce NOW client on the same connection.
2. Verify the external HDMI TV is connected **during the test**. If `dumpsys display` lists only the internal display, no external display is enumerated by Android.
3. Try SHIELD using 0.2.1; compare 1080p and 4K choices but verify the actual stream rather than menu entries.
4. If it still closes, collect a log **before launching the game**, not only after teardown:

```sh
su -c 'am force-stop com.nvidia.tegrazone3; logcat -c'
su -c '(logcat -b main -b system -b crash -v threadtime -f /sdcard/Download/gfn-live.txt >/dev/null 2>&1 & echo $! > /data/local/tmp/gfn-live.pid)'
# Start GFN and immediately try opening a game. After it exits:
su -c 'kill $(cat /data/local/tmp/gfn-live.pid)'
```

Review the file for secrets/identifiers before sharing it. Do not change systemwide Qualcomm, codec, or DRM properties.

## Observed symptoms, before 0.2.1

A user's log showed `frameCount=0`, `codec=UNKNOWN`, `networkSessionId=NOT_SET`, `result=0x80320004`, client label `0x8032000B`, and a mobile 5G connection. The logfile **started during stream teardown**, so the original initialization failure was not captured. Their `dumpsys display` reported only internal 1200x2670 display. Neither establishes a decoder fault or a server rejection on its own.

## Uninstall / rollback

Disable this LSPosed module in the LSPosed manager and force-stop the SHIELD app. If required, reinstall 0.2.0 for comparison. All changes are limited to the GFN process except for the separately installed systemwide KernelSU feature module.
