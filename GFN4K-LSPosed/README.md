# GFN4K LSPosed ATV Spoof 0.2.0

Experimental, reversible per-process identity spoof for `com.nvidia.tegrazone3` on rooted Android. Built with the legacy Xposed API 82 using LSPosed.

## Build

On GitHub, open **Actions → Build GFN4K LSPosed APK** and run the workflow, or push changes to `GFN4K-LSPosed/` on `main`. Once the build is green, download the `GFN4K-ATV-LSPosed-0.2.0` artifact.

The build uses Java 17, Gradle 8.10.2 and Android Gradle Plugin 8.7.3. The Xposed API 82 is resolved at compile-time from its original repository `https://api.xposed.info/` and is **not packaged inside the APK**.

## Installation

Install the generated APK and enable it only for `com.nvidia.tegrazone3` in LSPosed. Keep the existing KernelSU feature module installed if required. Restart the target app and look for `GFN4K/ATV` in LSPosed logs.

## Limitations

This does not guarantee NVIDIA will authorize 4K streaming. Server entitlement, decoder capabilities and the active HDMI display may still limit supported modes. Restoring the previous state is as simple as disabling the LSPosed module and restarting the target app. This project was not validated on a physical Xiaomi 15 when committed.
