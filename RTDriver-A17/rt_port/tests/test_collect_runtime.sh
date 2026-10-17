#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/kernel" "$TMP/out" "$TMP/report"
SCRIPT="$ROOT/collect_runtime.sh"
SUFFIX='kernel_device_modules-6.12/rt_a17_diag/runtime/rt_a17/rt_a17.ko'
FAKE="$TMP/out/sandbox/execroot/_main/bazel-out/k8/bin/$SUFFIX"
mkdir -p "$(dirname "$FAKE")"
printf 'sandbox diagnostic fixture' > "$FAKE"
if bash "$SCRIPT" "$TMP/kernel" "$TMP/out" "$TMP/report" 2>/dev/null; then
  echo 'ERROR: accepted sandbox output' >&2; exit 1
fi
REAL="$TMP/out/output_base/execroot/_main/bazel-out/k8/bin/$SUFFIX"
mkdir -p "$(dirname "$REAL")"
printf 'canonical collector fixture, not a module' > "$REAL"
bash "$SCRIPT" "$TMP/kernel" "$TMP/out" "$TMP/report"
cmp "$REAL" "$TMP/report/rt_a17.ko"
OTHER="$TMP/out/other/execroot/_main/bazel-out/k8/bin/$SUFFIX"
mkdir -p "$(dirname "$OTHER")"
cp "$REAL" "$OTHER"
if bash "$SCRIPT" "$TMP/kernel" "$TMP/out" "$TMP/report" 2>/dev/null; then
  echo 'ERROR: accepted ambiguous outputs' >&2; exit 1
fi
mkdir -p "$TMP/kernel/bazel-bin/$(dirname "$SUFFIX")"
ln -s "$REAL" "$TMP/kernel/bazel-bin/$SUFFIX"
bash "$SCRIPT" "$TMP/kernel" "$TMP/out" "$TMP/report"
cmp "$REAL" "$TMP/report/rt_a17.ko"
echo 'Runtime collector canonical/sandbox/ambiguity/symlink checks passed.'
