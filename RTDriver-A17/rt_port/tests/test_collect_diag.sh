#!/usr/bin/env bash
# Offline regression: select only the canonical Bazel RT diagnostic artifact.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
KROOT="$TMP/Kernel/kernel"
KOUT="$TMP/Kernel/out"
CANON="$KOUT/bazel/output_user_root/output_base/execroot/_main/bazel-out/k8-fastbuild/bin/kernel_device_modules-6.12/rt_a17_diag/rt_a17_diag/rt_a17_diag.ko"
STAGED="$KOUT/bazel/output_user_root/output_base/sandbox/staging/rt_a17_diag.ko"
mkdir -p "$KROOT" "$(dirname "$CANON")" "$(dirname "$STAGED")"
printf 'CANONICAL' > "$CANON"
printf 'STALE' > "$STAGED"
bash "$ROOT/RTDriver-A17/rt_port/collect_diag.sh" "$KROOT" "$KOUT" "$TMP/report"
cmp "$CANON" "$TMP/report/rt_a17_diag.ko"
sha256sum -c "$TMP/report/rt-diag-sha256.txt" >/dev/null
rm "$CANON"
if bash "$ROOT/RTDriver-A17/rt_port/collect_diag.sh" "$KROOT" "$KOUT" "$TMP/bad" >"$TMP/err" 2>&1; then
  echo 'ERROR: stale sandbox file was accepted' >&2; exit 1
fi
echo 'A17 RT diagnostic collection tests PASSED (no kernel compilation).'
