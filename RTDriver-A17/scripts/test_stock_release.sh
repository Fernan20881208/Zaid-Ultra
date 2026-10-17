#!/usr/bin/env bash
# Zero-compile regression for exact Samsung CZI4 localversion insertion.
# The same expected release was independently reproduced with the original
# scripts/setlocalversion extracted from Samsung SM-A175F_16_Opensource.zip.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
KSRC="$TMP/kernel-6.12"
mkdir -p "$KSRC/scripts" "$KSRC/arch/arm64/configs"
printf "VERSION = 6\nPATCHLEVEL = 12\nSUBLEVEL = 38\n" > "$KSRC/Makefile"
printf 'CONFIG_LOCALVERSION="-4k"\n' > "$KSRC/arch/arm64/configs/gki_defconfig"
cat > "$KSRC/scripts/setlocalversion" <<'EOS'
#!/bin/sh
# CI fixture for localversion file ordering in stock Linux setlocalversion.
srctree="$1"
suffix="$(cat "$srctree"/localversion* 2>/dev/null)"
config_localversion="$(sed -n 's/^CONFIG_LOCALVERSION=//p' include/config/auto.conf)"
printf "%s%s%s\n" "$KERNELVERSION" "$suffix" "$config_localversion"
EOS
bash "$REPO/RTDriver-A17/scripts/configure_stock_release.sh" "$KSRC" > "$TMP/output"
grep -Fq "6.12.38-android16-5-abA175FXXS7CZI4-4k" "$TMP/output"
test "$(cat "$KSRC/localversion-a17-ci")" = "-abA175FXXS7CZI4"
# No extra suffix may be duplicated on subsequent calls.
bash "$REPO/RTDriver-A17/scripts/configure_stock_release.sh" "$KSRC" > /dev/null
printf "conflict\n" > "$KSRC/localversion-extra"
if bash "$REPO/RTDriver-A17/scripts/configure_stock_release.sh" "$KSRC" >"$TMP/error" 2>&1; then
  echo "ERROR: conflicting localversion file was accepted" >&2; exit 1
fi
grep -Fq "Unexpected localversion files" "$TMP/error"
echo "A17 stock localversion regression PASSED; no kernel compilation."
