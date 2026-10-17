#!/usr/bin/env bash
# Copy only the canonical Kleaf-generated SM-A175F compatibility module.
# This does not build, load, sign, patch vermagic or install any module.
set -euo pipefail
if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "Usage: collect_probe_artifact.sh KROOT REPORT_DIR [KLEAF_OUT_ROOT]" >&2
  exit 2
fi
KROOT="$(realpath "$1")"
REPDIR="$(realpath -m "$2")"
KOUT="${3:-$(dirname "$KROOT")/out}"
KOUT="$(realpath -m "$KOUT")"
mkdir -p "$REPDIR"
EXPECTED="$KROOT/bazel-bin/kernel_device_modules-6.12/a17_compat_probe/a17_compat_probe/a17_compat_probe.ko"
if [[ -s "$EXPECTED" ]]; then
  PICKED="$(realpath "$EXPECTED")"
else
  # Bazel with --output_root may place its symlink elsewhere. Only the
  # execroot bazel-out/bin product qualifies, never staging or sandbox_stash.
  [[ -d "$KOUT" ]] || { echo "Missing Kleaf output tree: $KOUT" >&2; exit 34; }
  mapfile -t candidates < <(find "$KOUT" -type f -size +0c \
    -path "*/execroot/_main/bazel-out/*/bin/kernel_device_modules-6.12/a17_compat_probe/a17_compat_probe/a17_compat_probe.ko" \
    | sort -u)
  if [[ "${#candidates[@]}" -ne 1 ]]; then
    printf "ERROR: expected exactly one canonical Bazel output, found %s\n" "${#candidates[@]}" >&2
    printf "candidate: %s\n" "${candidates[@]}" >&2
    exit 34
  fi
  PICKED="${candidates[0]}"
fi
[[ -f "$PICKED" && -s "$PICKED" ]] || { echo "Canonical module missing or empty: $PICKED" >&2; exit 34; }
printf "%s\n" "$PICKED" > "$REPDIR/probe-paths.txt"
cp -- "$PICKED" "$REPDIR/a17_compat_probe.ko"
sha256sum "$REPDIR/a17_compat_probe.ko" > "$REPDIR/probe-sha256.txt"
printf "Collected real Bazel module from: %s\n" "$PICKED"
