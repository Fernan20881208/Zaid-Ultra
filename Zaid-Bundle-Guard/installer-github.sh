#!/system/bin/sh
# Zaid Bundle Guard downloader - only encrypted artifacts; no tokens/keys.
set -eu
umask 077
EXPECTED_SHA=6f84227c3aaa97404aa56f39d4e39ba79f37ca89ec68f5fca002fcd1fa813dc0
EXPECTED_SIZE=68526006
DEFAULT_URL=https://github.com/Fernan20881208/Zaid-Ultra/releases/download/zaid-bundle-guard-v1.1/Zaid-Bundle-Guard-16-ENCRYPTED-v1.1.zip
URL="$DEFAULT_URL"
[ "$#" -eq 0 ] || URL="$1"
[ "$(id -u)" = 0 ] || { echo "Requiere root: su -c 'sh /ruta/installer-github.sh URL'"; exit 1; }
case "$URL" in https://*) ;; *) echo "Solo descargas HTTPS" >&2; exit 1;; esac
command -v sha256sum >/dev/null 2>&1 || { echo "Falta sha256sum"; exit 1; }
tmp=$(mktemp -d /data/local/tmp/.zaid-bundle-XXXXXXXX) || { echo "No se pudo crear temporal" >&2; exit 1; }
chmod 700 "$tmp"
trap 'rm -rf -- "$tmp"' 0
trap 'exit 1' 1 2 3 15
zip="$tmp/bundle.zip"
if command -v curl >/dev/null 2>&1; then
  curl -fL --retry 2 --connect-timeout 15 --max-time 600 -o "$zip" "$URL"
elif command -v wget >/dev/null 2>&1; then
  wget -O "$zip" "$URL"
elif command -v busybox >/dev/null 2>&1; then
  busybox wget -O "$zip" "$URL"
else
  echo "Se necesita curl, wget o busybox" >&2
  exit 1
fi
[ "$(wc -c < "$zip" | tr -d ' ')" = "$EXPECTED_SIZE" ] || { echo "Tamaño distinto" >&2; exit 1; }
sum=$(sha256sum "$zip")
hash=$(printf '%s\n' "$sum" | cut -d' ' -f1)
[ "$hash" = "$EXPECTED_SHA" ] || { echo "SHA-256 inválido" >&2; exit 1; }
if command -v magisk >/dev/null 2>&1; then
  magisk --install-module "$zip"
elif command -v ksud >/dev/null 2>&1; then
  ksud module install "$zip"
elif [ -x /data/adb/ksu/bin/ksud ]; then
  /data/adb/ksu/bin/ksud module install "$zip"
elif [ -x /data/adb/ksu/ksud ]; then
  /data/adb/ksu/ksud module install "$zip"
else
  echo "No se encontró Magisk/KernelSU" >&2
  exit 1
fi
echo "Bóveda cifrada instalada. Se eliminan descargas temporales."
echo "Ninguno de los 16 módulos internos se instala automáticamente."
echo "Configura tu servidor de licencias para activar e instalar módulos individuales."
