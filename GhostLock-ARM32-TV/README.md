# ARM32 TV Inspector v0.1.0

Android 12 (API 31) and Android TV compatible diagnostics. Java-only APK; works with a 32-bit (`armeabi-v7a`) Android userspace. The app shows the Android version, security patch, ABI lists, kernel and root availability.

**Not a root exploit and does not grant privileges.** The 5.4.284 kernel of the target TV has not been validated for any 32-bit rooting method.

Build from the repository root with `cd GhostLock-ARM32-TV && gradle :app:assembleDebug`, or download the APK from GitHub Actions artifacts after this branch's workflow completes.

No root required to install; supports Android TV remote navigation and smartphone touch.
