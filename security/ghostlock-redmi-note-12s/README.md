# GhostLock / CVE-2026-43499 — Redmi Note 12S audit

## Target reported by device

- Model: 2303ERA42L
- Device: ocean
- Android: 13 / SDK 33
- Firmware fingerprint: Redmi/ocean_global/ocean:13/TP1A.220624.014/V14.0.7.0.THZMIXM:user/release-keys
- Kernel release: 4.19.191-g1617daf5074d
- Architecture: aarch64

## Status: not confirmed; no flashable patch

The version string and Android security patch date do not establish whether this kernel is vulnerable. Xiaomi may backport fixes without changing the upstream version. The running kernel configuration is not readable from the reported unprivileged shell, and the public source branch has not yet been matched to the exact firmware build.

Do not treat a source-level hunk drafted without the exact source as a verified fix. Do not flash a kernel or run a proof-of-concept that can panic the device based on this report alone.

## Required before preparing a kernel patch

1. Obtain the exact Xiaomi kernel source/tag/commit matching firmware `V14.0.7.0.THZMIXM`.
2. Inspect `kernel/locking/rtmutex.c` and its call sites against the upstream CVE fix and stable backports.
3. Port the minimal upstream fix to the exact 4.19 tree, preserving that tree's APIs and locking semantics.
4. Build with the matching defconfig/toolchain and run static checks plus relevant kernel tests.
5. Test on a recoverable device with a stock boot image available; only then consider distributing a boot image.

## Device-only, non-destructive metadata

Run in Termux:

```sh
uname -a
getprop ro.build.fingerprint
getprop ro.build.version.security_patch
getprop ro.product.device
```

This gathers metadata only; it does not establish exploitability.
