#!/usr/bin/env bash
# Compare external .ko symbol CRCs with the exact Module.symvers from the
# compiled Samsung kernel. This does NOT verify ABI of the running device.
set -euo pipefail
if [[ $# -ne 2 ]]; then echo "usage: verify_module_crc.sh MODULE_KO KERNEL_MODULE_SYMVERS" >&2; exit 2; fi
KO="$(realpath "$1")"
SYMVERS="$(realpath "$2")"
[[ -s "$KO" && -s "$SYMVERS" ]] || { echo "Missing ELF module or Module.symvers" >&2; exit 3; }
command -v modprobe >/dev/null || { echo "kmod modprobe is required for CRC verification" >&2; exit 4; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
modprobe --dump-modversions "$KO" > "$TMP/module-crc.txt"
[[ -s "$TMP/module-crc.txt" ]] || { echo "No __versions CRC entries found" >&2; exit 5; }
awk 'NF >= 2 {print $2, tolower($1)}' "$TMP/module-crc.txt" | sort -u > "$TMP/module-sorted.txt"
awk 'NF >= 3 {print $2, tolower($1)}' "$SYMVERS" | sort -u > "$TMP/kernel-sorted.txt"
while read -r sym crc; do
  [[ -n "$sym" && -n "$crc" ]] || continue
  result="$(awk -v sym="$sym" '$1==sym {print $2}' "$TMP/kernel-sorted.txt")"
  if [[ -z "$result" ]]; then echo "ERROR: Kernel Module.symvers missing referenced symbol $sym" >&2; exit 6; fi
  if [[ "$result" != "$crc" ]]; then
    printf "ERROR: CRC mismatch for %s: module=%s kernel=%s\n" "$sym" "$crc" "$result" >&2; exit 7
  fi
  printf "CRC match: %s %s\n" "$sym" "$crc"
done < "$TMP/module-sorted.txt"
echo "Module CRCs match the build's own Module.symvers only; DEVICE CRCs not verified."
