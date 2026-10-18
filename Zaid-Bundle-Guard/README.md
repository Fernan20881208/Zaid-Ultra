# Zaid Bundle Guard – Instalación desde GitHub

El script `installer-github.sh` descarga por HTTPS un ZIP cifrado de 16 módulos, valida **tamaño y SHA-256**, solicita al instalador oficial de Magisk o KernelSU que lo instale y elimina su archivo temporal al terminar (también en caso de error). No requiere ni publica las claves privadas.

**Importante:** el ZIP remoto es un *asset* de GitHub Release que debe publicarse **por separado**. En este commit **no se ha subido ningún archivo ZIP** ni se ha creado la release, así que la URL predeterminada no funcionará hasta subir el asset correcto. Puede pasarse una URL HTTPS alternativa como primer argumento. El ZIP cifrado tiene SHA-256 `6f84227c3aaa97404aa56f39d4e39ba79f37ca89ec68f5fca002fcd1fa813dc0`.

Ejecutar desde Termux cuando la release exista:

```sh
curl -fL https://raw.githubusercontent.com/Fernan20881208/Zaid-Ultra/main/Zaid-Bundle-Guard/installer-github.sh -o /data/local/tmp/zaid-installer.sh
su -c 'sh /data/local/tmp/zaid-installer.sh'
```

Para URL alternativa:

```sh
su -c 'sh /data/local/tmp/zaid-installer.sh https://servidor-ejemplo.com/Zaid-Bundle-Guard-16-ENCRYPTED-v1.1.zip'
```

Después de instalar y reiniciar, debe configurarse el **servidor HTTPS de licencias real**. El ejecutor incluido admite esta configuración sin recompilar:

```sh
su -c '/data/adb/modules/zaid_bundle_guard/bin/zaid-bundle config-server https://tuservidor.com'
su -c '/data/adb/modules/zaid_bundle_guard/bin/zaid-bundle list'
```

Para activar sin dejar la licencia en el historial:

```sh
read -r LIC
printf '%s\n' "$LIC" | su -c '/data/adb/modules/zaid_bundle_guard/bin/zaid-bundle activate'
unset LIC
```

Instalar un único paquete autorizado:

```sh
su -c '/data/adb/modules/zaid_bundle_guard/bin/zaid-bundle install encore ksu'
```

## Límites importantes

- La protección AES-256-GCM solo protege la **distribución**: una vez instalados, los módulos originales siguen siendo accesibles a root.
- El ZIP de demostración no incluye servidor de licencias operativo. Debe configurarse un servidor HTTPS y contar con claves **privadas** que correspondan al paquete cifrado.
- No publiques `server-data/package_keys.json`, claves de activación, tokens GitHub ni archivos administradores.
- La rama `main` de este repositorio es **pública**; no se han publicado los 16 ZIP originales ni los datos secretos.
- Conserva los autores y licencias de cada módulo y comprueba permisos de redistribución. No se instalan los 16 módulos automáticamente: puede haber incompatibilidades.
- Pruebas locales: simulación de Magisk, verificación SHA-256, integridad de los 16 payloads y eliminación de temporales. No está probado en un Android real.
