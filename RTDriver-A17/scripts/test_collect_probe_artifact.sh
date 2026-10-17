#!/usr/bin/env bash
# Offline fixture validating GitHub Actions #39041316430 output path.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
KROOT="$TMP/Kernel/kernel"
KOUT="$TMP/Kernel/out"
CANON="$KOUT/bazel/output_user_root/output_base/execroot/_main/bazel-out/k8-fastbuild/bin/kernel_device_modules-6.12/a17_compat_probe/a17_compat_probe/a17_compat_probe.ko"
STAGED="$KOUT/bazel/output_user_root/output_base/execroot/_main/bazel-out/k8-fastbuild/bin/kernel_device_modules-6.12/a17_compat_probe/a17_compat_probe/staging/lib/modules/6.12.38-android16-5-4k/extra/kernel_device_modules-6.12/a17_compat_probe/a17_compat_probe.ko"
SANDBOX="$KOUT/bazel/output_user_root/output_base/sandbox/sandbox_stash/KernelModule/123/execroot/_main/out/android16-6.12/kernel_device_modules-6.12/a17_compat_probe/a17_compat_probe.ko"
mkdir -p "$KROOT" "$(dirname "$CANON")" "$(dirname "$STAGED")" "$(dirname "$SANDBOX")"
printf "CANONICAL-ELF-FIXTURE" > "$CANON"
printf "STAGING-NOT-CANONICAL" > "$STAGED"
printf "SANDBOX-NOT-CANONICAL" > "$SANDBOX"
ln -s "$(dirname "$(dirname "$(dirname "$(dirname "$CANON")")")")" "$KROOT/bazel-bin"
# Force the verified fallback route: Bazel symlink can be absent with --output_root.
rm "$KROOT/bazel-bin"
bash "$REPO/RTDriver-A17/scripts/collect_probe_artifact.sh" "$KROOT" "$TMP/report1" "$KOUT"
cmp "$CANON" "$TMP/report1/a17_compat_probe.ko"
grep -Fxq "$CANON" "$TMP/report1/probe-paths.txt"
sha256sum -c "$TMP/report1/probe-sha256.txt" >/dev/null
# Symlink route must also choose the one canonical nested Bazel output.
ln -s "$KOUT/bazel/output_user_root/output_base/execroot/_main/bazel-out/k8-fastbuild/bin" "$KROOT/bazel-bin"
bash "$REPO/RTDriver-A17/scripts/collect_probe_artifact.sh" "$KROOT" "$TMP/report2" "$KOUT"
cmp "$CANON" "$TMP/report2/a17_compat_probe.ko"
rm "$KROOT/bazel-bin"
SECOND="$KOUT/bazel/output_user_root/output_base/execroot/_main/bazel-out/opt/bin/kernel_device_modules-6.12/a17_compat_probe/a17_compat_probe/a17_compat_probe.ko"
mkdir -p "$(dirname "$SECOND")"
printf "AMBIGUOUS" > "$SECOND"
if bash "$REPO/RTDriver-A17/scripts/collect_probe_artifact.sh" "$KROOT" "$TMP/report3" "$KOUT" >"$TMP/err.log" 2>&1; then
  echo "ERROR: collector accepted multiple canonical outputs" >&2
  exit 1
fi
grep -Fq "expected exactly one canonical Bazel output, found 2" "$TMP/err.log"
echo "A17 probe path collection regression PASSED without kernel compilation."
