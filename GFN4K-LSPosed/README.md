# GFN4K LSPosed ATV Spoof 0.2.0

Experimental per-process spoof for `com.nvidia.tegrazone3` on Xiaomi 15. Requires LSPosed and the separate SHIELD feature module. Does **not** guarantee 4K streaming: NVIDIA's entitlement service and decoder/display negotiation can still limit resolution.

Build via GitHub Actions. Scope the module **only** to GeForce NOW for SHIELD TV. Disable the module if the client crashes.

This repository copy uses an Xposed API compile-time dependency from Maven; if unavailable, supply a compatible compile-only Xposed API JAR at `app/libs/xposed-api-compile-only-stubs.jar` and switch `app/build.gradle` accordingly.
