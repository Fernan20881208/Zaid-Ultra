#!/system/bin/sh
# ZAID MODULE GUARD - selector interactivo. Ejecutar con su -c 'sh menu.sh'.
set -u
CLI=/data/adb/modules/zaid_bundle_guard/bin/zaid-bundle
[ "$(id -u)" = 0 ] || { echo 'Necesitas root'; exit 1; }
[ -x "$CLI" ] || { echo 'Instala primero Zaid Bundle Guard'; exit 1; }
"$CLI" list >/dev/null 2>&1 || { echo 'No se puede verificar el manifiesto'; exit 1; }
sel=''; mgr=''
id_for() {
 case "$1" in
 1) echo encore;;2) echo COPG;;3) echo zygisk_vector;;4) echo zygisksu;;
 5) echo DisableFPSLimit;;6) echo yc_dfps;;7) echo CuRefreshRateTuner;;
 8) echo YAKT;;9) echo zram-swap-manager;;10) echo unityfix;;
 11) echo saturation;;12) echo flagsecurepatcher;;13) echo systemui-bootloop;;
 14) echo iOS_Emoji;;15) echo DynamicSystemTweaks-arm64;;16) echo kp_enhancer;;
 esac
}
name_for() {
 case "$1" in
 1) echo 'Encore Tweaks';;2) echo 'COPG SPOOF';;3) echo 'Zygisk Vector';;
 4) echo 'Zygisk Next';;5) echo 'Disable FPS Limit';;6) echo 'Dfps';;
 7) echo 'CuRefreshRateTuner';;8) echo 'YAKT';;9) echo 'zRAM Swap Manager';;
 10) echo 'UnityFix';;11) echo 'Saturation';;12) echo 'FlagSecurePatcher';;
 13) echo 'SystemUI Bootloop Saver';;14) echo 'iOS Emoji';;
 15) echo 'DynamicSystemTweaks-arm64';;16) echo 'KnoxPatch Enhancer';;
 esac
}
marked() { case " $sel " in *" $1 "*) return 0;;*) return 1;;esac; }
pause_menu() { printf 'Enter para continuar...'; IFS= read -r wait_input || :; }
set_manager() {
 k=0;m=0
 if command -v ksud >/dev/null 2>&1 || [ -x /data/adb/ksu/bin/ksud ] || [ -x /data/adb/ksu/ksud ]; then k=1;fi
 if command -v magisk >/dev/null 2>&1;then m=1;fi
 if [ "$k" -eq 1 ] && [ "$m" -eq 0 ];then mgr=ksu
 elif [ "$m" -eq 1 ] && [ "$k" -eq 0 ];then mgr=magisk
 elif [ "$k" -eq 1 ] && [ "$m" -eq 1 ];then
  printf '1) KernelSU  2) Magisk: '; IFS= read -r ch || return 1
  case "$ch" in 1)mgr=ksu;;2)mgr=magisk;;*)echo 'Opcion invalida';return 1;;esac
 else mgr='';echo 'No se detecto un gestor.';fi
}
toggle() {
 values=$(printf '%s' "$1" | tr ',;+' '   ')
 set -- $values
 [ "$#" -gt 0 ] || { echo 'Seleccion vacia';return 1; }
 for x in "$@";do case "$x" in [1-9]|1[0-6]) :;;*)echo "Numero invalido: $x";return 1;;esac;done
 for x in "$@";do
  if marked "$x";then
   new=''
   for y in $sel;do [ "$y" = "$x" ] || new="$new $y";done
   sel="$new"
  else sel="$sel $x";fi
 done
}
preflight() {
 if marked 3 && marked 4;then echo 'No selecciones Vector y Zygisk Next juntos.';return 1;fi
 if marked 16;then
  vendor=$(getprop ro.product.manufacturer 2>/dev/null | tr '[:upper:]' '[:lower:]')
  [ "$vendor" = samsung ] || { echo 'KnoxPatch Enhancer requiere Samsung.';return 1; }
 fi
 if marked 15;then
  cpu=''
  if [ -r /sys/devices/system/cpu/possible ];then IFS= read -r cpu </sys/devices/system/cpu/possible || :;fi
  [ "$cpu" = 0-5 ] || { echo 'DynamicSystemTweaks-arm64 requiere seis nucleos.';return 1; }
 fi
 if marked 3 || marked 4;then echo 'ATENCION: comprueba si ya tienes Zygisk activo.';fi
 if marked 1 && marked 8;then echo 'ATENCION: Encore y YAKT pueden interferir.';fi
 if marked 5 || marked 6 || marked 7;then echo 'ATENCION: algunos ajustes de FPS pueden entrar en conflicto.';fi
}
verify() {
 [ -n "$sel" ] || { echo 'Marca primero los modulos';return 1; }
 result=0
 for x in $sel;do
  id=$(id_for "$x")
  echo "Verificando $(name_for "$x") ($id)"
  "$CLI" verify "$id" || result=1
 done
 [ "$result" -eq 0 ] || { echo 'Fallo una verificacion. No se instalara nada.';return 1; }
 echo 'Verificacion satisfactoria.'
}
install_sel() {
 [ -n "$sel" ] || { echo 'Sin seleccion';return 1; }
 [ -n "$mgr" ] || { echo 'Elige gestor con G';return 1; }
 preflight || return 1
 verify || return 1
 echo 'Se instalaran estos modulos:'
 for x in $sel;do echo " - $(name_for "$x")";done
 printf 'Escribe INSTALAR para confirmar: '
 IFS= read -r reply || return 1
 [ "$reply" = INSTALAR ] || { echo 'Cancelado';return 1; }
 for x in $sel;do
  id=$(id_for "$x")
  echo "Instalando: $id"
  "$CLI" install "$id" "$mgr" || { echo "Fallo $id; se detienen las instalaciones siguientes.";return 1; }
 done
 sel=''
 echo 'Terminado. Revisa el gestor y reinicia manualmente.'
}
set_manager || :
while :;do
 if [ -t 1 ] && command -v clear >/dev/null 2>&1;then clear;fi
 result=$("$CLI" status 2>&1)
 case "$result" in
 *'Licencia almacenada'*) state='GUARDADA - validacion online al usar';;
 *'Sin licencia activada'*) state='SIN ACTIVAR';;
 *) state='DESCONOCIDA';;
 esac
 echo '+------------------------------------------------+'
 echo '|               ZAID MODULE GUARD                |'
 echo '|                Protected Modules               |'
 echo '+------------------------------------------------+'
 echo " LICENCIA: $state"
 echo " MODULOS: 16 | GESTOR: $mgr"
 x=1
 while [ "$x" -le 16 ];do
  mark=' '
  marked "$x" && mark=x
  id=$(id_for "$x")
  installed=''
  if [ -d "/data/adb/modules/$id" ] || [ -d "/data/adb/modules_update/$id" ];then installed=' [instalado]';fi
  printf ' [%s] %2s) %-28s%s\n' "$mark" "$x" "$(name_for "$x")" "$installed"
  x=$((x+1))
 done
 echo '--------------------------------------------------'
 echo 'Numeros: marcar/desmarcar (1,3,5)'
 echo 'A) Instalar  V) Verificar  S) Estado  L) Lista'
 echo 'G) Gestor   C) Limpiar    0) Salir'
 printf 'Opcion: '
 IFS= read -r choice || break
 case "$choice" in
 [Aa])install_sel;pause_menu;;[Vv])verify;pause_menu;;
 [Ss])"$CLI" status;pause_menu;;[Ll])"$CLI" list;pause_menu;;
 [Gg])set_manager;pause_menu;;[Cc])sel='';;
 0|[Qq])echo 'Saliendo sin reiniciar';break;;
 *)toggle "$choice" || pause_menu;;
 esac
done
