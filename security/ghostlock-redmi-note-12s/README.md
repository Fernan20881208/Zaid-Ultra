# GhostLock / CVE-2026-43499 — Redmi Note 12S

## Reported device

- Model: `2303ERA42L`
- Device/codename: `ocean`
- Android: 13 / SDK 33
- Firmware fingerprint: `Redmi/ocean_global/ocean:13/TP1A.220624.014/V14.0.7.0.THZMIXM:user/release-keys`
- Reported kernel: `4.19.191-g1617daf5074d`
- Architecture: `aarch64`

## Source-level backport

The public Xiaomi kernel branch `MiCode/Xiaomi_Kernel_OpenSource:sea-t-oss` has the vulnerable code pattern in `kernel/locking/rtmutex.c`: `remove_waiter()` locks `current->pi_lock`, clears `current->pi_blocked_on`, and passes `current` to `rt_mutex_adjust_prio_chain()`. That is incorrect for proxy-lock rollback, where `waiter->task` may differ from `current`.

This branch's public source file had blob SHA `a5ec4f68527e533fb25e447ed18e2efd179a21b5` when this backport was prepared. The patch is adapted from the upstream Linux fix:

- Upstream commit: [3bfdc63936dd4773109b7b8c280c0f3b5ae7d349](https://kernel.googlesource.com/pub/scm/linux/kernel/git/i3c/linux/+/3bfdc63936dd4773109b7b8c280c0f3b5ae7d349)
- CVE advisory: [CVE-2026-43499](https://www.cve.org/CVERecord?id=CVE-2026-43499)
- Patch: [0001-rtmutex-use-waiter-task.patch](patches/0001-rtmutex-use-waiter-task.patch)

The 4.19 backport uses the tree's existing `raw_spin_lock()` API instead of newer `scoped_guard` syntax.

### What the patch changes

1. Captures `waiter->task` as `waiter_task`.
2. Locks `waiter_task->pi_lock` while dequeuing and clears `waiter_task->pi_blocked_on`.
3. Passes `waiter_task` to `rt_mutex_adjust_prio_chain()`.

## Validation status and limits

GitHub Actions workflow `.github/workflows/ghostlock-patch-check.yml` downloads the public `sea-t-oss` reference file and checks that the patch applies and the expected changes are present. This is a patch-applicability check, **not** a full kernel compilation or runtime test.

The exact Xiaomi source revision corresponding to firmware `V14.0.7.0.THZMIXM` has not been independently identified. Therefore this source backport must not be described as a verified fix for the installed binary until the exact vendor source is matched and the kernel is built and tested with the matching configuration/toolchain.

## Apply to a kernel source tree

From the root of a local kernel checkout, first check applicability:

```sh
git apply --check security/ghostlock-redmi-note-12s/patches/0001-rtmutex-use-waiter-task.patch
```

Only if the check passes, apply the patch:

```sh
git apply security/ghostlock-redmi-note-12s/patches/0001-rtmutex-use-waiter-task.patch
git diff --check
git diff -- kernel/locking/rtmutex.c
```

Do not force the patch if it fails to apply. A matching kernel source tree, defconfig, Android toolchain, and boot-image recovery plan are required before a device build should be attempted.

## Device metadata (Termux; non-destructive)

```sh
uname -a
getprop ro.build.fingerprint
getprop ro.build.version.security_patch
getprop ro.product.device
```

Metadata alone cannot prove whether a patch is present in the running kernel.
