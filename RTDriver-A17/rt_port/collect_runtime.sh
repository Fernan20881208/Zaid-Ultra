#!/usr/bin/env bash
# Preserve the exact runtime module, even if subsequent verification fails.
set -euo pipefail
if [[ $# -ne 3 ]]; then
  echo 'usage: collect_runtime.sh KERNEL_ROOT OUTPUT_ROOT REPORT_DIR' >&2; exit 2
fi
KROOT="$(realpath "$1")"
KOUT="$(realpath "$2")"
REPORT="$(realpath -m "$3")"
mkdir -p "$REPORT"
SUFFIX='kernel_device_modules-6.12/rt_a17_diag/runtime/rt_a17/rt_a17.ko'
EXPECTED="$KROOT/bazel-bin/$SUFFIX"
if [[ -s "$EXPECTED" ]]; then
  FOUND="$(realpath "$EXPECTED")"
else
  mapfile -t matches < <(find "$KOUT" -type f -size +0c \
    -path "*/execroot/_main/bazel-out/*/bin/$SUFFIX" \
    ! -path '*/sandbox/*' | sort -u)
  if [[ "${#matches[@]}" -ne 1 ]]; then
    echo "ERROR: expected one canonical rt_a17.ko, found ${#matches[@]}" >&2; exit 34
  fi
  FOUND="${matches[0]}"
fi
cp -- "$FOUND" "$REPORT/rt_a17.ko"
printf '%s\n' "$FOUND" > "$REPORT/rt-port-source-path.txt"
sha256sum "$REPORT/rt_a17.ko" > "$REPORT/rt-port-sha256.txt"
echo 'Collected partial RT port; original application and device execution unverified.'
