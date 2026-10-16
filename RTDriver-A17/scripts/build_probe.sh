#!/usr/bin/env bash
# Fail-closed build: ONLY against prepared exact Samsung kernel.
set -euo pipefail
TARGET='6.12.38-android16-5-abA175FXXS7CZI4-4k'
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
KDIR="${1:-}"
if [[ -z "$KDIR" || ! -d "$KDIR" ]]; then
  echo 'Uso: build_probe.sh /arbol/kernel-Samsung-preparado' >&2
  exit 2
fi
KDIR="$(realpath "$KDIR")"
for f in Makefile .config Module.symvers include/generated/utsrelease.h; do
  if [[ ! -s "$KDIR/$f" ]]; then
    echo "ERROR: Falta $KDIR/$f; no se puede validar ABI" >&2
    exit 3
  fi
done
RELEASE="$(sed -nE 's/^[[:space:]]*#define[[:space:]]+UTS_RELEASE[[:space:]]+"([^"]+)"/\1/p' "$KDIR/include/generated/utsrelease.h" | head -n 1)"
if [[ "$RELEASE" != "$TARGET" ]]; then
  echo "ERROR: KERNEL [$RELEASE] != [$TARGET]" >&2
  exit 4
fi
grep -qx 'CONFIG_MODULES=y' "$KDIR/.config" || { echo 'ERROR: CONFIG_MODULES=y requerido' >&2; exit 5; }
grep -qx 'CONFIG_MODVERSIONS=y' "$KDIR/.config" || { echo 'ERROR: CONFIG_MODVERSIONS=y requerido' >&2; exit 6; }
grep -qx 'CONFIG_CFI_CLANG=y' "$KDIR/.config" || { echo 'ERROR: CONFIG_CFI_CLANG=y requerido para esta configuración Samsung' >&2; exit 11; }
grep -qx 'CONFIG_MODULE_SIG=y' "$KDIR/.config" || { echo 'ERROR: CONFIG_MODULE_SIG=y requerido para esta configuración Samsung' >&2; exit 12; }
if ! grep -qx '# CONFIG_MODULE_SIG_FORCE is not set' "$KDIR/.config"; then
  echo 'ERROR: configuración MODULE_SIG_FORCE diferente de la capturada' >&2
  exit 13
fi
if ! grep -qx 'CONFIG_MODULE_SIG_ALL=y' "$KDIR/.config"; then
  echo 'ERROR: configuración MODULE_SIG_ALL diferente de la capturada' >&2
  exit 14
fi
if ! grep -qE '^0x[0-9A-Fa-f]+[[:space:]]+[^[:space:]]+[[:space:]]+' "$KDIR/Module.symvers"; then
  echo 'ERROR: Module.symvers no contiene CRCs en formato esperado; no reutilizar un archivo de otro kernel' >&2
  exit 15
fi
command -v clang >/dev/null || { echo 'ERROR: clang no está instalado' >&2; exit 7; }
make -C "$KDIR" M="$ROOT/src" ARCH=arm64 LLVM=1 modules
KO="$ROOT/src/a17_compat_probe.ko"
[[ -s "$KO" ]] || { echo 'ERROR: no se generó .ko' >&2; exit 8; }
if command -v readelf >/dev/null; then
  readelf -h "$KO" | grep -qE 'Machine:[[:space:]]*AArch64' || { echo 'ERROR: no es AArch64' >&2; exit 9; }
fi
if command -v modinfo >/dev/null; then
  VER="$(modinfo -F vermagic "$KO")"
  [[ "${VER%% *}" == "$TARGET" ]] || { echo "ERROR: vermagic=$VER" >&2; exit 10; }
fi
mkdir -p "$ROOT/out"
cp "$KO" "$ROOT/out/a17_compat_probe.ko"
sha256sum "$ROOT/out/a17_compat_probe.ko" > "$ROOT/out/sha256.txt"
echo 'OK: módulo de prueba generado; no instalado, NO es RT Driver.'
