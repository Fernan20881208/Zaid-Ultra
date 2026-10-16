# Samsung Galaxy A17 — base OSS integrada con CZI4 (verificado localmente)

Fecha: 2026-10-16. Equipo: SM-A175F, KernelSU, Android 16.

## Entradas verificadas

**Fuente base**: `SM-A175F_16_Opensource.zip`

- SHA-256: `4fa6e41f63151728277f49842d3fe1d59d6cfaaf853a161c7c2ed98f906727dd`
- ZIP: ~347 MB; contiene `Kernel.tar.gz`, `Platform.tar.gz`, `README_Kernel.txt`, `README_Platform.txt`.
- `Kernel.tar.gz`: 338,336,546 bytes comprimidos, 105,631 archivos regulares, ~1,952.6 MiB de datos.
- Fuente extraída en entorno de análisis: ~2.2 GB en disco.
- Linux fuente `kernel-6.12/Makefile`: `VERSION=6`, `PATCHLEVEL=12`, `SUBLEVEL=38`.
- Incluye `kernel/kernel_device_modules-6.12/build.sh`, `scripts/gen_build_config.py`, `arch/arm64/configs/mediatek-bazel_defconfig` y `mgk_64_k612_defconfig`.
- No incluye `Module.symvers` ya generado, y los toolchains externos de AOSP/Samsung no vienen completos en el ZIP. El ZIP *Platform* corresponde a módulos de espacio de usuario, no es necesario para nuestro probe inicial.

**Actualización exacta CZI4**: `SM-A175F_16_Opensource_A175FXXS7CZI4_A175NKSS7CZI2.zip`

- SHA-256: `643bde1afbf8dd17f98962314245818a0057ed46d6a22503f7ddeacd0478495b`
- TAR interior `A175FXXS7CZI4_kernel.tar.gz`: 21 archivos fuente.
- Aplicado **localmente** sobre las fuentes base: **21/21 rutas existentes**, 21 archivos distintos, 21 reemplazados; SHA-256 de cada salida verificado.
- Se guardó respaldo de los 21 archivos previos en el entorno de trabajo local.
- **No** se aplicó el ZIP DTS de la variante coreana SM-A175N.

## Configuración oficial generada

El generador entregado por Samsung se ejecutó satisfactoriamente con:

```bash
cd Kernel/kernel
python3 kernel_device_modules-6.12/scripts/gen_build_config.py \
  --kernel-defconfig mediatek-bazel_defconfig \
  --kernel-defconfig-overlays "entry_level.config sec_ogki_fragment.config mt6789_disable_mtk_charger_overlay.config mt6789_teegris_4_overlay.config" \
  --kernel-build-config-overlays "" -m user \
  -o ../out/target/product/a17/obj/KERNEL_OBJ/build.config
```

Salida creada: `Kernel/out/target/product/a17/obj/KERNEL_OBJ/build.config` (aproximadamente 1.3 KB). Este éxito **no implica** compilación del kernel.

Configuración del dispositivo aportada anteriormente:
- `CONFIG_MODULES=y`, `CONFIG_MODVERSIONS=y`, `CONFIG_MODULE_SIG=y`, `CONFIG_MODULE_SIG_ALL=y`, `# CONFIG_MODULE_SIG_FORCE is not set`.
- `CONFIG_CFI_CLANG=y`, `CONFIG_DEBUG_INFO_BTF=y`.
- Kernel exacto: `6.12.38-android16-5-abA175FXXS7CZI4-4k`, compiler Android Clang 19.0.1.

## Compilación pendiente

- Este entorno solo tiene Clang 17.0.0, no los prebuilts Android Clang 19.0.1 (r536225), y no cuenta con el árbol AOSP `prebuilts/` indicado en el README del paquete. `tools/bazel` apunta a un script incluido, pero ese script por sí solo no satisface las herramientas externas.
- El árbol combinado **todavía no se ha compilado** para ARM64. No hay `Module.symvers` del build CZI4 ni `.ko` del módulo de prueba verificado.
- Las fuentes de Samsung no contienen el código fuente original del RT Driver 6.1.112, por lo que una compilación kernel exitosa por sí sola **no porta** dicho driver.
- Es necesario reconstruir la cadena de herramientas Samsung/Kleaf/GKI, comparar ABI y CFI y generar símbolos versionados auténticos. No forzar `insmod`.

## Reproducción en Linux

```bash
bash RTDriver-A17/scripts/prepare_official_a17.sh \
  /ruta/SM-A175F_16_Opensource.zip \
  /ruta/SM-A175F_16_Opensource_A175FXXS7CZI4_A175NKSS7CZI2.zip \
  /ruta/de/salida-nueva
```

Acepta exclusivamente estos SHA-256, requiere Linux con `unzip`, `tar`, `bash`, `sha256sum` y `python3`, genera `build.config`, crea backup y no carga módulos. El procedimiento ya fue realizado sobre los archivos reales en el entorno local; el nuevo script integrado tendrá validación de CI de sintaxis, no una recompilación completa en GitHub.
