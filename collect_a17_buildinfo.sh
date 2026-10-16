#!/system/bin/sh
# Sólo lectura del kernel; crea TXT local. NO instala módulos ni reinicia.
PATH=/system/bin:/system/xbin:$PATH
OUT='/sdcard/Download/a17-buildinfo.txt'
{
  echo '=== BUILD TARGET ==='
  uname -r
  getprop ro.product.model
  getprop ro.build.version.release
  echo '=== ROOT ==='
  id
  echo '=== CONFIG DE KERNEL ==='
  if [ -r /proc/config.gz ]; then
    zcat /proc/config.gz 2>/dev/null | grep -E '^((# )?CONFIG_(MODULES|MODVERSIONS|MODULE_SIG|MODULE_SIG_FORCE|MODULE_SIG_ALL|MODULE_UNLOAD|KPROBES|KALLSYMS|DEBUG_INFO_BTF|LTO_CLANG|CFI_CLANG|TRIM_UNUSED_KSYMS|IKHEADERS|IKCONFIG|GKI)(=| is not set))'
  else
    echo '/proc/config.gz no accesible'
  fi
  echo '=== ARTEFACTOS LOCALES ==='
  for f in /sys/kernel/kheaders.tar.xz /sys/kernel/btf/vmlinux /proc/config.gz; do
    if [ -r "$f" ]; then ls -l "$f"; else echo "No encontrado: $f"; fi
  done
  echo '=== MODVERSIONS DISPONIBLES ==='
  for p in /lib/modules /vendor/lib/modules /vendor_dlkm/lib/modules /system_dlkm/lib/modules; do
    if [ -d "$p" ]; then
      find "$p" -name Module.symvers -o -name modules.symbols 2>/dev/null | head -n 25
    fi
  done
  echo '=== KHEADERS CONTENIDO RELEVANTE (si tar soporta xz) ==='
  if [ -r /sys/kernel/kheaders.tar.xz ] && command -v tar >/dev/null 2>&1; then
    tar -tf /sys/kernel/kheaders.tar.xz 2>/dev/null | grep -E '(^|/)(Module.symvers|utsrelease.h|autoconf.h|Kbuild|Makefile)$' | head -n 25
  fi
  echo '=== MODULOS CARGADOS (primeros 12) ==='
  head -n 12 /proc/modules
} > "$OUT" 2>&1
chmod 0644 "$OUT"
echo "Diagnostico adicional guardado en $OUT"
