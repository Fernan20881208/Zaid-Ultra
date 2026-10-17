#!/usr/bin/env bash
# kmod-output fixtures prove missing/mismatched CRCs cannot pass the verifier.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir "$TMP/bin"
printf 'fixture' > "$TMP/module.ko"
cat > "$TMP/bin/modprobe" <<'SH'
#!/bin/sh
cat "$CRC_FIXTURE"
SH
chmod +x "$TMP/bin/modprobe"
export PATH="$TMP/bin:$PATH" CRC_FIXTURE="$TMP/crc"
printf '0x12345678\tmodule_layout\n0x90abcdef\tmisc_register\n' > "$CRC_FIXTURE"
printf '0x12345678\tmodule_layout\tvmlinux\tEXPORT_SYMBOL\n0x90abcdef\tmisc_register\tvmlinux\tEXPORT_SYMBOL\n' > "$TMP/symvers"
bash "$ROOT/verify_module_crc.sh" "$TMP/module.ko" "$TMP/symvers" > "$TMP/good"
for issue in mismatch missing empty; do
  case "$issue" in
    mismatch) sed -i 's/0x90abcdef/0x11111111/' "$TMP/symvers";;
    missing) sed -i '/misc_register/d' "$TMP/symvers";;
    empty) : > "$CRC_FIXTURE";;
  esac
  if bash "$ROOT/verify_module_crc.sh" "$TMP/module.ko" "$TMP/symvers" > "$TMP/error" 2>&1; then
    echo "ERROR: CRC verification accepted $issue" >&2; exit 1
  fi
done
echo 'Symbol CRC match, mismatch, missing symbol and missing versions tests PASSED.'
