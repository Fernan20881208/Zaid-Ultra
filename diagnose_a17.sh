#!/system/bin/sh
# Ejecutar en el Samsung con: su -c 'sh /sdcard/Download/diagnose_a17.sh'
# Sólo lectura. NO instala drivers, NO borra dmesg, NO reinicia.
PATH=/system/bin:/system/xbin:$PATH
OUT=/sdcard/Download/a17-kernel-diagnostico.txt
{
 echo '=== DISPOSITIVO ==='
 getprop ro.product.model
 getprop ro.product.device
 getprop ro.product.board
 getprop ro.build.fingerprint
 getprop ro.build.version.release
 echo '=== KERNEL ==='
 uname -a
 cat /proc/version
 echo '=== ROOT ==='
 id
 echo '=== KCONFIG ==='
 if [ -r /proc/config.gz ]; then
   zcat /proc/config.gz 2>/dev/null | grep -E '^CONFIG_(MODULES|MODVERSIONS|KPROBES|KALLSYMS|MODULE_SIG|MODULE_SIG_FORCE|MODULE_UNLOAD|DEBUG_INFO_BTF|IKCONFIG_PROC)=' || true
 else echo 'No disponible /proc/config.gz'; fi
 echo '=== BTF Y SIMBOLOS ==='
 ls -l /sys/kernel/btf/vmlinux /sys/kernel/kheaders.tar.xz /proc/kallsyms 2>&1
 echo '=== MODULOS CARGADOS ==='
 cat /proc/modules 2>&1 | head -30
 echo '=== KERNEL LOG MODULOS ==='
 dmesg 2>&1 | grep -iE 'module|vermagic|unknown symbol|version magic|signature|exec format' | tail -n 45 || true
} > "$OUT" 2>&1
chmod 0644 "$OUT"
echo "Diagnostico guardado: $OUT"
