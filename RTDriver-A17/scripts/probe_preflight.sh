#!/usr/bin/env bash
# Zero-compilation validation of Samsung/MediaTek 6.12.38 Kleaf environment.
# Tested against the ORIGINAL SM-A175F source ZIP and generated build.config.
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo 'usage: probe_preflight.sh SAMSUNG_KERNEL_ROOT [BUILD_CONFIG]' >&2
  exit 2
fi
KROOT="$(realpath "$1")"
CONFIG="${2:-../out/target/product/a17/obj/KERNEL_OBJ/build.config}"
[[ -f "$KROOT/kernel_device_modules-6.12/kernel/kleaf/_setup_env.sh" ]] || {
  echo 'Missing original Samsung/MediaTek _setup_env.sh' >&2; exit 3;
}
[[ -f "$KROOT/$CONFIG" ]] || {
  echo 'Missing generated Samsung build.config' >&2; exit 4;
}
[[ -x "$KROOT/build/kernel/kleaf/workspace_status.sh" ]] || {
  echo 'Missing official Kleaf workspace_status.sh' >&2; exit 5;
}
(
  cd "$KROOT"
  export DEVICE_MODULES_DIR=kernel_device_modules-6.12
  export BUILD_CONFIG="$CONFIG" PROJECT=mgk_64_k612 MODE=user ARCH=arm64
  export OUT_DIR=../out/target/product/a17/obj/KLEAF_OBJ
  # These are the Bazel wrapper's original defaults for SCM calculation.
  export KLEAF_USE_KLEAF_LOCALVERSION=true
  # Samsung's build.config intentionally expands unset vars to empty.
  # Temporarily disable nounset; re-enable immediately after sourcing.
  set +u
  source ./kernel_device_modules-6.12/kernel/kleaf/_setup_env.sh >/dev/null
  set -u
  [[ "$KERNEL_VERSION" == kernel-6.12 ]]
  [[ "$KERNEL_BUILD_VARIANT" == user ]]
  [[ "$SOURCE_DATE_EPOCH" == 0 ]]
  [[ "$OUT_DIR" == /* ]]
  echo "Official environment: KERNEL_VERSION=$KERNEL_VERSION KERNEL_BUILD_VARIANT=$KERNEL_BUILD_VARIANT SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH"
  export KLEAF_REPO_MANIFEST="$KROOT:$KROOT/kernel_device_modules-6.12/fake_manifest.xml"
  result="$(./build/kernel/kleaf/workspace_status.sh)"
  echo "$result" | grep -q 'STABLE_SOURCE_DATE_EPOCHS.*"kernel-6.12": "0"'
  echo 'Samsung Kleaf workspace-status preflight PASSED. No kernel module was built.'
)
