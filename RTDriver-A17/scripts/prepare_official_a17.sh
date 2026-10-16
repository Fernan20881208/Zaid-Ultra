#!/usr/bin/env bash
# Unpack the exact verified SM-A175F Android 16 OSS base, overlay CZI4,
# and generate Samsung/MediaTek build.config. Never compiles, signs,
# inserts kernel modules, alters the device or flashes firmware.
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: bash $0 SM-A175F_16_Opensource.zip SM-A175F_16_Opensource_A175FXXS7CZI4_A175NKSS7CZI2.zip DEST_DIR" >&2
  exit 2
fi

BASE_ZIP="$(realpath "$1")"
UPDATE_ZIP="$(realpath "$2")"
DEST="$(realpath -m "$3")"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

BASE_SHA="4fa6e41f63151728277f49842d3fe1d59d6cfaaf853a161c7c2ed98f906727dd"
UPDATE_SHA="643bde1afbf8dd17f98962314245818a0057ed46d6a22503f7ddeacd0478495b"

for bin in unzip tar sha256sum python3 realpath; do
  command -v "$bin" >/dev/null || { echo "Missing dependency: $bin" >&2; exit 3; }
done
for zip in "$BASE_ZIP" "$UPDATE_ZIP"; do
  [[ -f "$zip" ]] || { echo "File not found: $zip" >&2; exit 4; }
done

# Only the exact archives inspected for this device are accepted.
[[ "$(sha256sum "$BASE_ZIP" | cut -d' ' -f1)" == "$BASE_SHA" ]] || {
  echo "BASE SHA-256 mismatch. Aborting." >&2; exit 5; }
[[ "$(sha256sum "$UPDATE_ZIP" | cut -d' ' -f1)" == "$UPDATE_SHA" ]] || {
  echo "CZI4 SHA-256 mismatch. Aborting." >&2; exit 6; }

# Avoid overwriting any existing tree or following a symlink into it.
if [[ -e "$DEST" || -L "$DEST" ]]; then
  echo "Destination already exists: $DEST. Select a fresh directory." >&2
  exit 7
fi
mkdir -p "$DEST/Kernel"
echo "[1/3] Extracting base sources (approx. 2.2 GB on disk)..."
unzip -p "$BASE_ZIP" Kernel.tar.gz | tar -xzf - --no-same-owner -C "$DEST/Kernel"

test -s "$DEST/Kernel/kernel-6.12/Makefile"
test -s "$DEST/Kernel/kernel/kernel_device_modules-6.12/scripts/gen_build_config.py"
grep -q '^VERSION = 6' "$DEST/Kernel/kernel-6.12/Makefile"
grep -q '^PATCHLEVEL = 12' "$DEST/Kernel/kernel-6.12/Makefile"
grep -q '^SUBLEVEL = 38' "$DEST/Kernel/kernel-6.12/Makefile"

echo "[2/3] Applying CZI4 patch with backup and manifest..."
python3 "$SCRIPT_DIR/apply_samsung_czi4.py" --update-zip "$UPDATE_ZIP" --base-dir "$DEST" --apply

echo "[3/3] Generating official Samsung/MediaTek build.config..."
(
  cd "$DEST/Kernel/kernel"
  python3 kernel_device_modules-6.12/scripts/gen_build_config.py \
    --kernel-defconfig mediatek-bazel_defconfig \
    --kernel-defconfig-overlays "entry_level.config sec_ogki_fragment.config mt6789_disable_mtk_charger_overlay.config mt6789_teegris_4_overlay.config" \
    --kernel-build-config-overlays "" \
    -m user -o ../out/target/product/a17/obj/KERNEL_OBJ/build.config
)
CONFIG="$DEST/Kernel/out/target/product/a17/obj/KERNEL_OBJ/build.config"
test -s "$CONFIG"

{
  echo "SM-A175F Samsung Open Source preparation"
  echo "Base ZIP SHA256=$BASE_SHA"
  echo "CZI4 ZIP SHA256=$UPDATE_SHA"
  echo "Kernel source=6.12.38"
  echo "Source package=SM-A175F_16_Opensource.zip"
  echo "Source overlay=A175FXXS7CZI4"
  echo "Config=$CONFIG"
  echo "Module.symvers=NOT BUILT"
  echo "Compiled=.ko NO"
  echo "Loaded on device=NO"
} > "$DEST/prepare-report.txt"

echo "Prepared source tree: $DEST/Kernel"
echo "Generated config: $CONFIG"
echo "WARNING: Full Samsung/AOSP Clang 19 prebuilts and valid kernel build/Module.symvers are still needed."
echo "No .ko was built, loaded, signed or installed."
