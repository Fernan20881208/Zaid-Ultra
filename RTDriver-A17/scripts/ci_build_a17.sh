#!/usr/bin/env bash
# Samsung SM-A175F 6.12.38 build attempt on GitHub Actions, no flashing/loading.
set -euo pipefail
if [[ $# -ne 3 ]]; then
  echo "Uso: bash ci_build_a17.sh BASE_ZIP CZI4_ZIP prepare|kernel|kernel_and_probe" >&2
  exit 2
fi
BASE_ZIP="$(realpath "$1")"
CZI4_ZIP="$(realpath "$2")"
MODE="$3"
case "$MODE" in prepare|kernel|kernel_and_probe) ;; *) echo "Modo invalido" >&2; exit 2;; esac
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTPUT="${A17_WORKDIR:-$RUNNER_TEMP/a17-build}"
REPORT="${A17_REPORT_DIR:-$REPO_ROOT/a17-ci-results}"
mkdir -p "$REPORT"
printf '%s\n' "Mode: $MODE" "Kernel target: 6.12.38-android16-5-abA175FXXS7CZI4-4k" > "$REPORT/status.txt"
df -h . > "$REPORT/disk-before.txt"
# The preparer verifies SHA256 of BOTH uploaded Samsung ZIPs; fail closed.
bash "$REPO_ROOT/RTDriver-A17/scripts/prepare_official_a17.sh" "$BASE_ZIP" "$CZI4_ZIP" "$OUTPUT"
cp "$OUTPUT/prepare-report.txt" "$REPORT/"
if [[ "$MODE" == "prepare" ]]; then
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
export PROJECT=mgk_64_k612 ARCH=arm64 MODE=user
export PATH="$(dirname "$CLANG_BIN"):$PATH"
{
  printf 'Selected clang: %s\n' "$CLANG_BIN"
  printf 'Kernel source: %s\n' "$OUTPUT/Kernel/kernel-6.12"
  printf 'Samsung target: %s\n' "$PROJECT"
} >> "$REPORT/status.txt"
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

if [[ "$MODE" == "kernel_and_probe" ]]; then
  SRC="$REPO_ROOT/RTDriver-A17/src"
  MOD="$KROOT/kernel_device_modules-6.12/a17_compat_probe"
  mkdir -p "$MOD"
  cp "$SRC/a17_compat_probe.c" "$SRC/Makefile" "$SRC/BUILD.bazel" "$MOD/"
  (
    cd "$KROOT"
    BAZEL_OUT="$(realpath -m "$KROOT/$OUT_DIR")"
    tools/bazel \
      --output_root="$BAZEL_OUT" \
      --output_base="$BAZEL_OUT/bazel/output_user_root/output_base" \
      build --config=stamp \
      --repo_manifest="${KROOT}/kernel_device_modules-6.12/fake_manifest.xml" \
      --//build/bazel_mgk_rules:kernel_version=6.12 \
      //kernel_device_modules-6.12/a17_compat_probe:a17_compat_probe
  ) 2>&1 | tee "$REPORT/probe-build.log"

  find "$OUTPUT/Kernel/out" -name 'a17_compat_probe.ko' -type f -print > "$REPORT/probe-paths.txt"
  if [[ ! -s "$REPORT/probe-paths.txt" ]]; then
    echo 'ERROR: Probe Bazel target did not produce a .ko' | tee -a "$REPORT/status.txt" >&2
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
df -h . > "$REPORT/disk-after.txt"
