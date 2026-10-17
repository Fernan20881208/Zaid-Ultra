#!/usr/bin/env bash
# Complete Samsung's missing //build/bazel_common_rules symlink target.
# The source is Google's real Bazel common rules, not a placeholder BUILD file.
set -euo pipefail
if [[ $# -ne 1 ]]; then
  echo 'Usage: fetch_bazel_common_rules.sh PATH_TO_SAMSUNG_KERNEL_ROOT' >&2
  exit 2
fi
KROOT="$(realpath "$1")"
[[ -f "$KROOT/build/bazel_mgk_rules/mgk.bzl" ]] || {
  echo "ERROR: Not a Samsung Mediatek kernel tree: $KROOT" >&2
  exit 3
}
LINK="$KROOT/build/bazel_common_rules"
[[ -L "$LINK" ]] || {
  echo "ERROR: expected original Samsung symlink, found other entry: $LINK" >&2
  exit 4
}
[[ "$(readlink "$LINK")" == "../../build/bazel_common_rules" ]] || {
  echo "ERROR: Samsung symlink points elsewhere: $(readlink "$LINK")" >&2
  exit 5
}
DEST="$(realpath -m "$LINK")"
EXPECTED="$(realpath -m "$(dirname "$KROOT")/build/bazel_common_rules")"
[[ "$DEST" == "$EXPECTED" ]] || {
  echo "ERROR: Refusing to write unexpected path: $DEST" >&2
  exit 6
}
PIN="b1a2840cb13746f94e4c88e7e28d308f51f87804"
URL="https://android.googlesource.com/platform/build/bazel_common_rules"
if [[ -d "$DEST/.git" ]]; then
  CURRENT="$(git -C "$DEST" rev-parse HEAD)"
  [[ "$CURRENT" == "$PIN" ]] || {
    echo "ERROR: Existing Bazel common rules commit $CURRENT != pinned $PIN" >&2
    exit 7
  }
else
  [[ ! -e "$DEST" ]] || { echo "ERROR: unmanaged destination exists: $DEST" >&2; exit 8; }
  mkdir -p "$(dirname "$DEST")"
  git clone --depth 1 --single-branch --branch main "$URL" "$DEST"
  CURRENT="$(git -C "$DEST" rev-parse HEAD)"
  [[ "$CURRENT" == "$PIN" ]] || {
    echo "ERROR: upstream revision changed: $CURRENT; expected $PIN" >&2
    exit 9
  }
fi
# Bazel needs the actual dist package, not just a .bzl file.
if [[ ! -f "$LINK/dist/dist.bzl" ]] || [[ ! -f "$LINK/dist/BUILD" && ! -f "$LINK/dist/BUILD.bazel" ]]; then
  echo "ERROR: dist.bzl or BUILD is missing in real AOSP Bazel rules" >&2
  exit 10
fi
echo "Verified Samsung symlink: $LINK -> $(readlink -f "$LINK")"
echo "Verified AOSP Bazel common rules commit: $CURRENT"
