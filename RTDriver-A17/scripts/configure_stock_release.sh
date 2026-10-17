#!/usr/bin/env bash
# Restore the observed Samsung A175F CZI4 kernel release via upstream Kbuild
# localversion* input, never by patching module ELF/vermagic.
set -euo pipefail
if [[ $# -ne 1 ]]; then echo "usage: configure_stock_release.sh KERNEL_SOURCE_DIR" >&2; exit 2; fi
KSRC="$(realpath "$1")"
EXPECTED="6.12.38-android16-5-abA175FXXS7CZI4-4k"
PREFIX="6.12.38-android16-5"
FIRMWARE_LOCALVERSION="-abA175FXXS7CZI4"
KFILE="$KSRC/localversion-a17-ci"
[[ -f "$KSRC/Makefile" && -f "$KSRC/scripts/setlocalversion" ]] || { echo "No Linux kernel source tree" >&2; exit 3; }
grep -Fxq "VERSION = 6" "$KSRC/Makefile"
grep -Fxq "PATCHLEVEL = 12" "$KSRC/Makefile"
grep -Fxq "SUBLEVEL = 38" "$KSRC/Makefile"
grep -Fxq 'CONFIG_LOCALVERSION="-4k"' "$KSRC/arch/arm64/configs/gki_defconfig" || {
  echo "Stock GKI config must use CONFIG_LOCALVERSION=-4k" >&2; exit 4;
}
# Reject alternate version fragments: blindly adding a label to a different
# source release would only create a misleading vermagic.
mapfile -t present < <(find "$KSRC" -maxdepth 1 -type f -name "localversion*" -print)
if (( ${#present[@]} > 0 )); then
  if (( ${#present[@]} != 1 )) || [[ "${present[0]}" != "$KFILE" ]] || [[ "$(cat "$KFILE")" != "$FIRMWARE_LOCALVERSION" ]]; then
    echo "Unexpected localversion files: refusing to combine conflicting release identifiers" >&2
    printf "Existing: %s\n" "${present[@]}" >&2
    exit 5
  fi
else
  printf "%s\n" "$FIRMWARE_LOCALVERSION" > "$KFILE"
fi
# Verify against the unmodified Samsung Linux scripts/setlocalversion.
# CONFIG_LOCALVERSION is read from include/config/auto.conf during Kbuild.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/include/config"
cp -- "$KFILE" "$TMP/localversion-a17-ci"
printf "CONFIG_LOCALVERSION=-4k\n# CONFIG_LOCALVERSION_AUTO is not set\n" > "$TMP/include/config/auto.conf"
actual="$(cd "$TMP" && KERNELVERSION="$PREFIX" LOCALVERSION="" sh "$KSRC/scripts/setlocalversion" "$TMP")"
if [[ "$actual" != "$EXPECTED" ]]; then
  printf "Kernel release mismatch expected=%s actual=%s\n" "$EXPECTED" "$actual" >&2
  exit 6
fi
printf "Samsung source localversion configured and reproduced: %s\n" "$actual"
printf "NOTICE: matching kernel release is NOT proof of device ABI/KMI or signature compatibility.\n"
