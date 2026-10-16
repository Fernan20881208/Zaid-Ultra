# Galaxy A17 SM-A175F — proyecto de compatibilidad RT Driver (fase 3)

**Objetivo:** Android 16, KernelSU, kernel `6.12.38-android16-5-abA175FXXS7CZI4-4k`.

**Estado (2026-10-16):** árbol Linux Samsung 6.12.38 completo **recibido, extraído y combinado** con los 21 cambios de CZI4, y `build.config` generado mediante la herramienta oficial. **No se ha compilado ningún .ko para el teléfono.** El `a17_compat_probe.c` de este repo es únicamente una prueba de compilación/carga básica, **no el RT Driver completo**.

## Fuentes oficiales verificadas

1. `SM-A175F_16_Opensource.zip` (SHA-256 `4fa6e41f63151728277f49842d3fe1d59d6cfaaf853a161c7c2ed98f906727dd`).
2. `SM-A175F_16_Opensource_A175FXXS7CZI4_A175NKSS7CZI2.zip` (SHA-256 `643bde1afbf8dd17f98962314245818a0057ed46d6a22503f7ddeacd0478495b`).

Las fuentes base son ~2.2 GiB al extraerse; no están subidas al repositorio GitHub porque el ZIP es grande. Se necesita descargar los originales desde Samsung Open Source. El paquete SM-A175N_KOR DTS no se debe mezclar automáticamente con SM-A175F.

[Informe del proceso real](reports/base-OSS-6.12.38-CZI4-integrated.md)

## Preparación de fuentes reproducible en Linux (no en Termux)

```bash
bash RTDriver-A17/scripts/prepare_official_a17.sh \
  /ruta/SM-A175F_16_Opensource.zip \
  /ruta/SM-A175F_16_Opensource_A175FXXS7CZI4_A175NKSS7CZI2.zip \
  /ruta/al/directorio-de-salida-nuevo
```

Este script verifica ambos SHA-256, descomprime las fuentes, aplica la actualización CZI4 con copia de seguridad y llama al generador oficial de `build.config`. **No compila ni instala el driver**.

El reporte de diagnóstico del dispositivo encontró:
- `CONFIG_MODULES=y`, `CONFIG_MODVERSIONS=y`, `CONFIG_MODULE_SIG=y`, `CONFIG_MODULE_SIG_ALL=y`, `# CONFIG_MODULE_SIG_FORCE is not set`.
- `CONFIG_CFI_CLANG=y`, `CONFIG_KPROBES=y`, `CONFIG_DEBUG_INFO_BTF=y`.
- KernelSU activo, BTF y `kheaders.tar.xz` presentes en el teléfono.

## Bloqueador para compilar .ko ARM64

La fuente oficial requiere toolchain/prebuilts externos (Clang Android 19.0.1 r536225, Bazel/Kleaf, AOSP/Samsung) y **un build correcto del kernel del equipo** para producir `Module.symvers` con CRC reales. Este entorno dispone de Clang 17 y no trae los prebuilts; por eso **no se intentó declarar que el driver es compatible**. Incluso al obtener `Module.symvers`, faltaría el código fuente del RT Driver original: el binario 6.1.112 no puede convertirse directamente en el driver 6.12 cambiando `vermagic`.

El módulo de prueba `src/a17_compat_probe.c` no implementa acceso a memoria ajena, breakpoints ni interfaces privilegiadas; permitirá comprobar el ABI solo cuando tengamos el entorno de build completo. Compilarlo requiere un árbol preparado con `Module.symvers` y `.config` auténticos:
```bash
bash RTDriver-A17/scripts/build_probe.sh /ruta/arbol-kernel-preparado
```

No hay instalaciones automáticas, `insmod`, reinicios, flasheos ni alteración de restricciones SELinux o firmas.


## Compilación en GitHub Actions — descarga desde MediaFire

El workflow [Build Galaxy A17 kernel 6.12 (MediaFire sources)](../../.github/workflows/a17-samsung-6.12-build.yml) **ya no necesita GitHub Releases ni adjuntar ZIPs manualmente**.

Fuentes configuradas (enlaces proporcionados por el propietario):
- Paquete combinado (actualización CZI4): https://www.mediafire.com/file/jwwfja7b8dqbswj/SM-A175F.zip/file
- Fuentes base: https://www.mediafire.com/file/dtt5mrk0wzevk9t/SM-A175F_16_Opensource.zip/file

El script `scripts/download_mediafire_sources.py` resuelve las páginas de descarga, obtiene ambos ZIP, comprueba SHA-256 **contra los archivos examinados**, extrae únicamente el ZIP incremental CZI4 de `SM-A175F.zip` y lo verifica. Rechaza archivos HTML, ZIPs incorrectos y cambios de contenido. No aplica el DTS de la variante coreana.

SHA-256 esperados:
- `SM-A175F.zip`: `8d05b158f0b393ef0f62771a5dc29f96298813a793cdcb0bd6acf442aa985b6a`.
- `SM-A175F_16_Opensource.zip`: `4fa6e41f63151728277f49842d3fe1d59d6cfaaf853a161c7c2ed98f906727dd`.
- ZIP incremental CZI4 extraído: `643bde1afbf8dd17f98962314245818a0057ed46d6a22503f7ddeacd0478495b`.

### Modos de compilación

- **push** a la definición del workflow o al descargador: se ejecuta **kernel_and_probe** automáticamente (intento completo), previa descarga y verificación de fuentes.
- **workflow_dispatch / prepare**: descarga, verifica SHA y prepara fuentes con 21 cambios CZI4.
- **workflow_dispatch / kernel**: adicionalmente intenta compilar el kernel Samsung, recuperar toolchain Clang 19.0.1 r536225 y producir `Module.symvers`.
- **workflow_dispatch / kernel_and_probe**: además intenta crear el `a17_compat_probe.ko` con Kleaf, una prueba de ABI **que no es el RT Driver**.

La salida se publica en artifacts de Actions bajo `a17-mediafire-build-report-<numero>` incluso si el trabajo falla.

**Advertencia:** MediaFire puede restringir las descargas automatizadas, exigir CAPTCHA o devolver HTML. Si ocurre, la descarga falla de forma explícita y hay que usar un origen de archivos que permita descargas sin interacción.

Las fuentes de Samsung no contienen el código fuente del RT Driver original y **no se ha verificado todavía un módulo funcional para el teléfono**. No se ejecutan flasheos, root, ni `insmod`.
