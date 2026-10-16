# Samsung source package — A175FXXS7CZI4

## Archivo recibido y examinado

`SM-A175F_16_Opensource_A175FXXS7CZI4_A175NKSS7CZI2.zip`

- ZIP SHA-256: `643bde1afbf8dd17f98962314245818a0057ed46d6a22503f7ddeacd0478495b`.
- Tamaño ZIP: 232900 bytes (aprox. 228 KiB).
- Contenido externo: `A175FXXS7CZI4_kernel.txt` y `A175FXXS7CZI4_kernel.tar.gz`.
- El tar.gz contiene **21 archivos regulares** y directorios.
- Los archivos son actualizaciones bajo `Kernel/kernel-6.12/`, `Kernel/kernel/kernel_device_modules-6.12/` y `Kernel/vendor/`; afectan HID, KVM, USB, GPU Mali, TIPC y MUIC.
- **NO** contiene `Module.symvers` ni el árbol de fuentes completo.
- **NO** contiene el código fuente del RT Driver.

## Instrucciones oficiales (parafraseadas)

El TXT incluido por Samsung requiere descargar primero el código fuente de **A175FXXU5CZE9**, actualizarlo con el contenido de **A175FXXS7CZI4**, y utilizar `kernel_device_modules-6.12/scripts/gen_build_config.py`.

Defconfig: `mediatek-bazel_defconfig`.

Overlays:
```
entry_level.config
sec_ogki_fragment.config
mt6789_disable_mtk_charger_overlay.config
mt6789_teegris_4_overlay.config
```

Variables:
```sh
export DEVICE_MODULES_DIR=kernel_device_modules-6.12
export BUILD_CONFIG=../out/target/product/a17/obj/KERNEL_OBJ/build.config
export OUT_DIR=../out/target/product/a17/obj/KLEAF_OBJ
export DIST_DIR=../out/target/product/a17/obj/KLEAF_OBJ/dist
export DEFCONFIG_OVERLAYS="entry_level.config sec_ogki_fragment.config mt6789_disable_mtk_charger_overlay.config mt6789_teegris_4_overlay.config"
export PROJECT=mgk_64_k612
export ARCH=arm64
export MODE=user
```
Ejecutar `./kernel_device_modules-6.12/build.sh` **solo tras disponer de todas las fuentes y toolchains**; no ejecutar en el teléfono.

## Herramienta añadida

`python3 RTDriver-A17/scripts/apply_samsung_czi4.py --update-zip /ruta/paquete_CZI4.zip`

Muestra el hash e inventario de los 21 archivos sin modificar nada.

`python3 RTDriver-A17/scripts/apply_samsung_czi4.py --update-zip /ruta/paquete_CZI4.zip --base-dir /ruta/al/directorio-que-contiene-Kernel`

Comprueba que están los directorios y scripts básicos del paquete base. **No modifica**.

Una vez confirmada la fuente base:

`python3 RTDriver-A17/scripts/apply_samsung_czi4.py --update-zip /ruta/paquete_CZI4.zip --base-dir /ruta/al/directorio-que-contiene-Kernel --apply`

Copia los 21 archivos encima de la fuente base y guarda copia de seguridad de los anteriores. Este paso se hace en Linux/Work, no con root ni en el teléfono.

## Bloqueador

Se requiere el archivo de **fuentes abiertas** de Samsung A175FXXU5CZE9. El firmware completo Odin de ese número no equivale al paquete de código fuente y no se debe usar para reemplazarlo.

Buscarlo en https://opensource.samsung.com/ con modelo `SM-A175F` y versión `A175FXXU5CZE9`. No se ha verificado un enlace directo al ZIP de fuentes. No se ha generado aún `Module.symvers` ni un `.ko` probado para SM-A175F.

## Comprobaciones posteriores

Tras combinar los paquetes, preparar/compilar con el flujo específico de Samsung, localizar `Module.symvers` y confirmar CRC reales, configuración CFI y release exacto `6.12.38-android16-5-abA175FXXS7CZI4-4k`. La coincidencia del release es necesaria pero no suficiente para garantizar identidad binaria/KMI.
