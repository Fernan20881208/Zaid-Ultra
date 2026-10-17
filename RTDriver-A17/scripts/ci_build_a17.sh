#!/usr/bin/env bash
# Samsung SM-A175F 6.12.38 build attempt on GitHub Actions, no flashing/loading.
set -euo pipefail
if [[ $# -ne 3 ]]; then
  echo "Uso: bash ci_build_a17.sh BASE_ZIP CZI4_ZIP prepare|preflight|kernel|kernel_and_probe" >&2
  exit 2
fi
BASE_ZIP="$(realpath "$1")"
CZI4_ZIP="$(realpath "$2")"
A17_STAGE="$3"
case "$A17_STAGE" in prepare|preflight|kernel|kernel_and_probe) ;; *) echo "Modo invalido" >&2; exit 2;; esac
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTPUT="${A17_WORKDIR:-$RUNNER_TEMP/a17-build}"
REPORT="${A17_REPORT_DIR:-$REPO_ROOT/a17-ci-results}"
mkdir -p "$REPORT"
printf '%s\n' "Build stage: $A17_STAGE" "Kernel target: 6.12.38-android16-5-abA175FXXS7CZI4-4k" > "$REPORT/status.txt"
df -h . > "$REPORT/disk-before.txt"
# The preparer verifies SHA256 of BOTH uploaded Samsung ZIPs; fail closed.
bash "$REPO_ROOT/RTDriver-A17/scripts/prepare_official_a17.sh" "$BASE_ZIP" "$CZI4_ZIP" "$OUTPUT"
cp "$OUTPUT/prepare-report.txt" "$REPORT/"
if [[ "$A17_STAGE" == "prepare" ]]; then
  echo "Source overlay prepared successfully. No compilation requested." | tee -a "$REPORT/status.txt"
  exit 0
fi

# Fetch the matching AOSP Android 16 / Linux 6.12 manifest dependencies.
# The Samsung source tree itself is NOT replaced by upstream Android common.
AOSP="$OUTPUT/aosp-deps"
mkdir -p "$AOSP"
REPO_BIN="$OUTPUT/repo"
curl --fail --location --retry 3 -sS https://storage.googleapis.com/git-repo-downloads/repo -o "$REPO_BIN"
chmod 0755 "$REPO_BIN"
(
  cd "$AOSP"
  "$REPO_BIN" init --depth=1 -u https://android.googlesource.com/kernel/manifest -b common-android16-6.12 --no-repo-verify
  "$REPO_BIN" sync --current-branch --no-tags --no-clone-bundle --fail-fast -j 3 \
    prebuilts/clang/host/linux-x86 \
    prebuilts/clang-tools \
    prebuilts/build-tools \
    prebuilts/kernel-build-tools \
    prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8 \
    prebuilts/jdk/jdk11 \
    prebuilts/rust \
    prebuilts/ndk-r26 \
    external/libcap external/libcap-ng \
    external/bazel-contrib-bazel_features external/bazel-skylib \
    external/bazelbuild-bazel-central-registry external/bazelbuild-platforms \
    external/bazelbuild-rules_cc external/bazelbuild-rules_license \
    external/bazelbuild-rules_pkg external/bazelbuild-rules_python \
    external/bazelbuild-rules_rust external/bazelbuild-rules_shell \
    external/python/absl-py \
    external/lz4 external/pigz external/toybox external/zlib external/zopfli \
    external/dtc external/elfutils \
    tools/mkbootimg
)
KROOT="$OUTPUT/Kernel/kernel"
# Samsung's KROOT/external links point to ../../external.
# Integrate into the real targets, preserving Kleaf build and vendor sources.
bash "$REPO_ROOT/RTDriver-A17/scripts/integrate_aosp_deps.sh" "$AOSP" "$KROOT" \
  2>&1 | tee "$REPORT/aosp-integration.log"
# Samsung ships build/bazel_common_rules as a symlink to an external checkout.
# The upstream Android 16 manifest no longer declares this legacy repository.
bash "$REPO_ROOT/RTDriver-A17/scripts/fetch_bazel_common_rules.sh" "$KROOT" \
  2>&1 | tee "$REPORT/bazel-common-rules.log"
# Bazel's kernel_toolchains rules require real bindgen prebuilts from AOSP.
test -f "$KROOT/prebuilts/clang-tools/BUILD.bazel" || {
  echo 'ERROR: AOSP prebuilts/clang-tools/BUILD.bazel not present' | tee -a "$REPORT/status.txt" >&2
  exit 37
}
test -f "$KROOT/prebuilts/clang-tools/linux-x86/bin/bindgen" || {
  echo 'ERROR: AOSP bindgen missing in prebuilts/clang-tools/linux-x86/bin' | tee -a "$REPORT/status.txt" >&2
  exit 38
}
echo 'Validated AOSP clang-tools with Bazel BUILD.bazel and bindgen.' | tee -a "$REPORT/status.txt"
CLANG_BIN="$KROOT/prebuilts/clang/host/linux-x86/clang-r536225/bin/clang"
if [[ ! -x "$CLANG_BIN" ]]; then
  echo "ERROR: AOSP manifest no entrego clang-r536225. No compilar con otra version." | tee -a "$REPORT/status.txt" >&2
  exit 31
fi
"$CLANG_BIN" --version | tee "$REPORT/clang-version.txt"
grep -q "clang version 19.0.1" "$REPORT/clang-version.txt" || { echo "Clang no coincide con el firmware" >&2; exit 32; }

# Device build uses the official Samsung/MediaTek build.sh and generated config.
export DEVICE_MODULES_DIR=kernel_device_modules-6.12
export BUILD_CONFIG=../out/target/product/a17/obj/KERNEL_OBJ/build.config
export OUT_DIR=../out/target/product/a17/obj/KLEAF_OBJ
export DIST_DIR=../out/target/product/a17/obj/KLEAF_OBJ/dist
export DEFCONFIG_OVERLAYS="entry_level.config sec_ogki_fragment.config mt6789_disable_mtk_charger_overlay.config mt6789_teegris_4_overlay.config"
# Samsung's MODE=user controls its variant and must never overwrite A17_STAGE.
export PROJECT=mgk_64_k612 ARCH=arm64 MODE=user
export PATH="$(dirname "$CLANG_BIN"):$PATH"
{
  printf 'Selected clang: %s\n' "$CLANG_BIN"
  printf 'Kernel source: %s\n' "$OUTPUT/Kernel/kernel-6.12"
  printf 'Samsung target: %s\n' "$PROJECT"
} >> "$REPORT/status.txt"

# Preflight the probe target and the ORIGINAL Samsung workspace-status command
# before starting the expensive kernel build. --nobuild analyzes Bazel rules
# but does not compile C/C++, build kernel images, or emit any .ko.
if [[ "$A17_STAGE" == "preflight" || "$A17_STAGE" == "kernel_and_probe" ]]; then
  SRC="$REPO_ROOT/RTDriver-A17/src"
  MOD="$KROOT/kernel_device_modules-6.12/a17_compat_probe"
  mkdir -p "$MOD"
  cp "$SRC/a17_compat_probe.c" "$SRC/Makefile" "$SRC/BUILD.bazel" "$MOD/"
  bash "$REPO_ROOT/RTDriver-A17/scripts/probe_preflight.sh" "$KROOT" "$BUILD_CONFIG" \
    2>&1 | tee "$REPORT/probe-source-preflight.log"
  (
    cd "$KROOT"
    set +u
    source ./kernel_device_modules-6.12/kernel/kleaf/_setup_env.sh > "$REPORT/probe-analysis-env.log" 2>&1
    set -u
    [[ "$KERNEL_VERSION" == "kernel-6.12" ]]
    [[ "$KERNEL_BUILD_VARIANT" == "user" ]]
    [[ "$SOURCE_DATE_EPOCH" == "0" ]]
    tools/bazel \
      --output_root="$OUT_DIR" \
      --output_base="$OUT_DIR/bazel/output_user_root/output_base" \
      build --nobuild --config=stamp \
      --repo_manifest="$KROOT:$KROOT/kernel_device_modules-6.12/fake_manifest.xml" \
      --experimental_writable_outputs \
      --allow_ddk_unsafe_headers=1 \
      --workaround_btrfs_b292212788 \
      --//build/bazel_mgk_rules:kernel_version=6.12 \
      --experimental_optimize_ddk_config_actions \
      --user_ddk_unsafe_headers=//kernel_device_modules-6.12:mtk_use_gki_unsafe_headers \
      //kernel_device_modules-6.12/a17_compat_probe:a17_compat_probe
  ) 2>&1 | tee "$REPORT/probe-analysis.log"
  echo "Bazel probe analysis passed without compilation." | tee -a "$REPORT/status.txt"
  if [[ "$A17_STAGE" == "preflight" ]]; then
    echo "Preflight completed; intentionally no kernel or .ko built." | tee -a "$REPORT/status.txt"
    exit 0
  fi
fi

(
  cd "$KROOT"
  bash ./kernel_device_modules-6.12/build.sh
) 2>&1 | tee "$REPORT/samsung-build.log"

# Search for actual build products, not fabricated .ko or CRCs.
find "$OUTPUT/Kernel/out" -name Module.symvers -type f -size +0c -print > "$REPORT/symvers-paths.txt"
find "$OUTPUT/Kernel/out" -name '*.ko' -type f -print > "$REPORT/kernel-modules-paths.txt"
if [[ ! -s "$REPORT/symvers-paths.txt" ]]; then
  echo 'ERROR: Build finished but no nonempty Module.symvers found' | tee -a "$REPORT/status.txt" >&2
  exit 33
fi
cp "$(head -n 1 "$REPORT/symvers-paths.txt")" "$REPORT/Module.symvers"
echo 'Samsung kernel build generated Module.symvers' | tee -a "$REPORT/status.txt"

if [[ "$A17_STAGE" == "kernel_and_probe" ]]; then
  SRC="$REPO_ROOT/RTDriver-A17/src"
  MOD="$KROOT/kernel_device_modules-6.12/a17_compat_probe"
  mkdir -p "$MOD"
  cp "$SRC/a17_compat_probe.c" "$SRC/Makefile" "$SRC/BUILD.bazel" "$MOD/"
  # MediaTek's kernel_device_modules-6.12/kernel/kleaf/_setup_env.sh sets
  # KERNEL_VERSION=kernel-6.12 and KERNEL_BUILD_VARIANT=user. Its build.sh
  # runs in a subshell above, so that environment was not inherited here.
  # The mgk_info repository rule requires KERNEL_VERSION and splits at '-'.
  export KERNEL_VERSION="kernel-6.12"
  export KERNEL_BUILD_VARIANT="$MODE"
  echo "Probe Bazel environment: KERNEL_VERSION=$KERNEL_VERSION, KERNEL_BUILD_VARIANT=$KERNEL_BUILD_VARIANT" | tee -a "$REPORT/status.txt"
  (
    cd "$KROOT"
    # Official Samsung build.sh sources _setup_env.sh before Bazel.
    # Source it here too; SOURCE_DATE_EPOCH=0 avoids missing Git metadata.
    # Samsung build.config requires temporarily disabling nounset.
    set +u
    source ./kernel_device_modules-6.12/kernel/kleaf/_setup_env.sh > "$REPORT/probe-env.log" 2>&1
    set -u
    [[ "$KERNEL_VERSION" == "kernel-6.12" ]]
    [[ "$KERNEL_BUILD_VARIANT" == "user" ]]
    [[ "$SOURCE_DATE_EPOCH" == "0" ]]
    [[ "$OUT_DIR" == /* ]]
    echo "Probe environment verified: KERNEL_VERSION=$KERNEL_VERSION SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH"
    BAZEL_OUT="$OUT_DIR"
    tools/bazel \
      --output_root="$BAZEL_OUT" \
      --output_base="$BAZEL_OUT/bazel/output_user_root/output_base" \
      build --config=stamp \
      --repo_manifest="$KROOT:$KROOT/kernel_device_modules-6.12/fake_manifest.xml" \
      --experimental_writable_outputs \
      --allow_ddk_unsafe_headers=1 \
      --workaround_btrfs_b292212788 \
      --//build/bazel_mgk_rules:kernel_version=6.12 \
      --experimental_optimize_ddk_config_actions \
      --user_ddk_unsafe_headers=//kernel_device_modules-6.12:mtk_use_gki_unsafe_headers \
      //kernel_device_modules-6.12/a17_compat_probe:a17_compat_probe
  ) 2>&1 | tee "$REPORT/probe-build.log"

  # Bazel-bin is the canonical output symlink; do not pick an arbitrary .ko.
  PROBE_BAZEL_BIN="$KROOT/bazel-bin/kernel_device_modules-6.12/a17_compat_probe/a17_compat_probe.ko"
  if [[ -s "$PROBE_BAZEL_BIN" ]]; then
    realpath "$PROBE_BAZEL_BIN" > "$REPORT/probe-paths.txt"
  else
    find "$OUTPUT/Kernel/out" -name 'a17_compat_probe.ko' -type f -size +0c -print | sort -u > "$REPORT/probe-paths.txt"
  fi
  if [[ "$(wc -l < "$REPORT/probe-paths.txt")" -ne 1 ]]; then
    echo 'ERROR: Expected exactly one real, nonempty a17_compat_probe.ko' | tee -a "$REPORT/status.txt" >&2
    exit 34
  fi
  cp "$(head -n 1 "$REPORT/probe-paths.txt")" "$REPORT/a17_compat_probe.ko"
  file "$REPORT/a17_compat_probe.ko" | tee "$REPORT/probe-file.txt"
  if ! readelf -h "$REPORT/a17_compat_probe.ko" | grep -q 'AArch64'; then
    echo 'ERROR: Probe not ELF AArch64' | tee -a "$REPORT/status.txt" >&2
    exit 35
  fi
  if command -v modinfo >/dev/null; then
    modinfo "$REPORT/a17_compat_probe.ko" | tee "$REPORT/probe-modinfo.txt"
    MAGIC="$(modinfo -F vermagic "$REPORT/a17_compat_probe.ko")"
    TARGET="6.12.38-android16-5-abA175FXXS7CZI4-4k"
    [[ "${MAGIC%% *}" == "$TARGET" ]] || { echo "ERROR: vermagic=$MAGIC" | tee -a "$REPORT/status.txt"; exit 36; }
  fi
  sha256sum "$REPORT/a17_compat_probe.ko" > "$REPORT/probe-sha256.txt"
  echo 'ARM64 probe produced, NOT loaded or proven compatible in device.' | tee -a "$REPORT/status.txt"
fi
if [[ "$A17_STAGE" == "kernel_and_probe" && ! -s "$REPORT/a17_compat_probe.ko" ]]; then
  echo 'ERROR: kernel_and_probe requested but there is no compiled compatibility probe.' | tee -a "$REPORT/status.txt" >&2
  exit 39
fi
df -h . > "$REPORT/disk-after.txt"
