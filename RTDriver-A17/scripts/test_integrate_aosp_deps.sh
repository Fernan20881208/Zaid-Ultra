#!/usr/bin/env bash
# Offline fixture: verify that Samsung's relative external symlinks resolve
# after copying AOSP into its expected directories.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
KROOT="$TEMP/samsung/Kernel/kernel"
PARENT="$(dirname "$KROOT")"
AOSP="$TEMP/aosp"
mkdir -p "$KROOT"/{build/kernel,kernel_device_modules-6.12,external,tools}
mkdir -p "$AOSP"/{external,prebuilts,tools/mkbootimg}
for d in toybox bazel-skylib lz4 zlib; do
  mkdir -p "$AOSP/external/$d"
  printf 'fixture' > "$AOSP/external/$d/smoke.txt"
  ln -s "../../external/$d" "$KROOT/external/$d"
done
mkdir -p "$AOSP/prebuilts/build-tools/linux_musl-x86/bin"
touch "$AOSP/prebuilts/build-tools/linux_musl-x86/bin/py3-cmd"
printf '#!/bin/sh\n' > "$KROOT/build/kernel/bazel.sh"
chmod +x "$KROOT/build/kernel/bazel.sh"
ln -s '../build/kernel/bazel.sh' "$KROOT/tools/bazel"
ln -s '../../system/tools/mkbootimg' "$KROOT/tools/mkbootimg"
echo 'fixture' > "$AOSP/tools/mkbootimg/smoke.txt"
bash "$ROOT/integrate_aosp_deps.sh" "$AOSP" "$KROOT"
for d in toybox bazel-skylib lz4 zlib; do
  cmp "$AOSP/external/$d/smoke.txt" "$KROOT/external/$d/smoke.txt"
done
[[ -f "$PARENT/system/tools/mkbootimg/smoke.txt" ]]
[[ -f "$KROOT/tools/mkbootimg/smoke.txt" ]]
[[ ! -d "$KROOT/external/toybox/.repo" ]]
echo 'Offline Samsung symlink and AOSP layout tests passed.'
