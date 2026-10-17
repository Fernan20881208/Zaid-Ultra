#!/usr/bin/env bash
# Collect only the canonical Samsung Kleaf rt_a17_diag output.
set -euo pipefail
if [[ $# -ne 3 ]]; then
  echo "usage: collect_diag.sh KERNEL_ROOT OUTPUT_ROOT REPORT_DIR" >&2; exit 2
fi
KROOT="$(realpath "$1")"
KOUT="$(realpath "$2")"
REPORT="$(realpath -m "$3")"
mkdir -p "$REPORT"
EXPECTED="$KROOT/bazel-bin/kernel_device_modules-6.12/rt_a17_diag/rt_a17_diag/rt_a17_diag.ko"
if [[ -s "$EXPECTED" ]]; then
  FOUND="$(realpath "$EXPECTED")"
else
  mapfile -t matches < <(find "$KOUT" -type f -size +0c \
    -path "*/execroot/_main/bazel-out/*/bin/kernel_device_modules-6.12/rt_a17_diag/rt_a17_diag/rt_a17_diag.ko" \
    ! -path '*/sandbox/*' | sort -u)
  if [[ "${#matches[@]}" -ne 1 ]]; then
    echo "ERROR: expected one rt_a17_diag.ko in canonical Bazel output, found ${#matches[@]}" >&2
    exit 34
  fi
  FOUND="${matches[0]}"
fi
[[ -s "$FOUND" ]] || { echo "Invalid RT diagnostic ELF output" >&2; exit 34; }
cp -- "$FOUND" "$REPORT/rt_a17_diag.ko"
printf "%s\n" "$FOUND" > "$REPORT/rt-diag-source-path.txt"
sha256sum "$REPORT/rt_a17_diag.ko" > "$REPORT/rt-diag-sha256.txt"
echo "Collected RT ABI diagnostic module. NOT the original RT driver."
