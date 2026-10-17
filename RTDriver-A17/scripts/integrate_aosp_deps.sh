#!/usr/bin/env bash
# Integrate downloaded AOSP prebuilts into a Samsung SM-A175F tree.
# Samsung's kernel/external/* entries are symlinks to ../../external/*.
# Samsung's kernel/tools/mkbootimg points to ../../system/tools/mkbootimg.
set -euo pipefail
if [[ $# -ne 2 ]]; then
  echo 'Usage: integrate_aosp_deps.sh AOSP_REPO_ROOT SAMSUNG_KERNEL_DIR' >&2
  exit 2
fi
AOSP="$(realpath "$1")"
KROOT="$(realpath "$2")"
PARENT="$(dirname "$KROOT")"
[[ -d "$KROOT/build/kernel" && -d "$KROOT/kernel_device_modules-6.12" ]] || {
  echo "Invalid Samsung kernel root: $KROOT" >&2; exit 3; }
[[ -d "$AOSP/external" && -d "$AOSP/prebuilts" ]] || {
  echo "AOSP prebuilts/external missing: $AOSP" >&2; exit 4; }
command -v rsync >/dev/null || { echo 'rsync missing' >&2; exit 5; }

# Prebuilts are resolved directly from KROOT by Kleaf bazel.sh.
mkdir -p "$KROOT/prebuilts"
rsync -a --exclude='.git' --ignore-existing "$AOSP/prebuilts/" "$KROOT/prebuilts/"
if [[ ! -f "$KROOT/prebuilts/clang-tools/BUILD.bazel" || ! -f "$KROOT/prebuilts/clang-tools/linux-x86/bin/bindgen" ]]; then
  echo "ERROR: Clang-tools BUILD.bazel or bindgen missing from synced prebuilts." >&2
  exit 9
fi

# Critical: DO NOT copy to KROOT/external. That contains relative symlinks.
# Its symlinks resolve to the *parent* of KROOT (PARENT/external).
mkdir -p "$PARENT/external"
rsync -a --exclude='.git' --ignore-existing "$AOSP/external/" "$PARENT/external/"

# Samsung's KROOT/tools/mkbootimg is ../../system/tools/mkbootimg.
if [[ -d "$AOSP/tools/mkbootimg" ]]; then
  mkdir -p "$PARENT/system/tools/mkbootimg"
  rsync -a --exclude='.git' --ignore-existing "$AOSP/tools/mkbootimg/" "$PARENT/system/tools/mkbootimg/"
fi

for d in toybox bazel-skylib lz4 zlib; do
  [[ -d "$KROOT/external/$d" ]] || {
    echo "Broken Samsung external link: $KROOT/external/$d" >&2
    exit 6
  }
done
[[ -x "$KROOT/tools/bazel" ]] || { echo 'Samsung Bazel launcher missing' >&2; exit 7; }
[[ -f "$KROOT/prebuilts/build-tools/linux_musl-x86/bin/py3-cmd" ]] || {
  echo 'Missing Bazel launcher prebuilt py3-cmd' >&2; exit 8; }

echo 'AOSP dependencies integrated in the paths expected by Samsung/Kleaf.'
echo 'No kernel modules built or installed by this script.'
