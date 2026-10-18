# Zaid Module Guard — Menú de selección

## Ejecutar en Termux

Para una instalación existente (KernelSU o Magisk) descarga el script:

```sh
curl -fL https://raw.githubusercontent.com/Fernan20881208/Zaid-Ultra/main/Zaid-Bundle-Guard/menu.sh -o /sdcard/Download/Zaid-Menu-Modulos.sh
su -c 'sh /sdcard/Download/Zaid-Menu-Modulos.sh'
```

También hay un ZIP instalable v1.1.3 que incluye el menú bajo `/data/adb/modules/zaid_bundle_guard/menu.sh`. En ese caso:

```sh
su -c 'sh /data/adb/modules/zaid_bundle_guard/menu.sh'
```

Introduce `1,3,5` para marcar/desmarcar; `V` verifica; `A` verifica e instala uno por uno tras escribir `INSTALAR`; `G` elige el gestor; `C` limpia; `0` sale.

El menú llama al ejecutable existente `zaid-bundle`: necesita una licencia activa para verificar o instalar, y las 16 claves AES siguen en Supabase.

La interfaz muestra `GUARDADA` en vez de `ACTIVA` cuando solo detecta la licencia almacenada: su vigencia real se comprueba online.

**Precauciones:** no instala automáticamente los 16 ZIP, no reinicia ni modifica DNS; se detiene ante fallos de verificación. Bloquea instalar dos implementaciones Zygisk seleccionadas juntas; no permite KnoxPatch fuera de Samsung ni el módulo de seis núcleos fuera de la configuración declarada. Aun así, verifica compatibilidad con tu dispositivo, conserva copias de seguridad y respeta las licencias de autores de terceros.

Script y ZIP probados localmente con un cliente simulado y validación de integridad; no se ha probado la instalación de esos módulos sobre un teléfono físico.
