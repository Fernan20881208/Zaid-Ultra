# Galaxy A17 SM-A175F — proyecto de compatibilidad RT Driver (fase 3)

**Objetivo:** Android 16, KernelSU, kernel `6.12.38-android16-5-abA175FXXS7CZI4-4k`.

**Port parcial compilado:** `rt_a17.ko` v0.2, separado del diagnóstico, construido
en Actions #39075095531: ELF AArch64, vermagic exacto y 60 CRC coincidentes con
el kernel generado, sin firma. Implementa READ_MEM, WRITE_MEM, MODULE_BASE y
breakpoints de un disparo; INIT_KEY y GYRO_CONFIG siguen sin implementar. El
teléfono y la integración APK siguen sin validar.
[Informe corregido y descarga](reports/rt-port-build-39075095531.md) ·
[Alcance del port](rt_port/runtime/README.md).

**Resultado actual (2026-10-17):** `rt_a17_diag.ko` v0.2 compilado y verificado
como ELF relocatable AArch64 en Actions #39058053061. Vermagic completo exacto,
8 CRC coincidentes con el Module.symvers generado, sin firma. Es solo diagnóstico;
ABI/KMI del teléfono, aceptación de firma y carga siguen sin probarse.
[Informe, hashes y artefacto real](reports/rt-diag-build-39058053061.md).

Los apartados siguientes conservan el historial de preparación y pruebas.

**Estado de preparación (2026-10-16):** árbol Linux Samsung 6.12.38 completo **recibido, extraído y combinado** con los 21 cambios de CZI4, y `build.config` generado mediante la herramienta oficial. **No se ha compilado ningún .ko para el teléfono.** El `a17_compat_probe.c` de este repo es únicamente una prueba de compilación/carga básica, **no el RT Driver completo**.

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

**Actualización 2026-10-17:** el build de fuentes oficiales Linux 6.12.38 con Clang Android 19.0.1 (r536225) **ya terminó exitosamente** en GitHub Actions #39000648253 y #39000654437, generando `Module.symvers` auténtico y numerosos módulos de kernel Samsung. Todavía **no** tenemos un `a17_compat_probe.ko` compilado/verificado ni prueba de KMI/ABI idéntico al kernel instalado. Falta el código fuente del RT Driver original 6.1.112; no se porta modificando `vermagic`.

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

- Las modificaciones normales de código **no** inician compilaciones completas.
  Una solicitud explícita nueva `build-requests/rt-diag-*.json` puede iniciar
  una sola ejecución `kernel_and_rt_diag`; primero exige pruebas estáticas y
  análisis Bazel `--nobuild`. También se conserva `workflow_dispatch`.
- **Preflight independiente**: [a17-probe-preflight.yml](../../.github/workflows/a17-probe-preflight.yml) solo analiza el objetivo Bazel con `--nobuild`; no ejecuta la compilación del kernel.
- **workflow_dispatch / preflight** (predeterminado): descarga, prepara, verifica el entorno Samsung y ejecuta análisis Bazel `--nobuild`, sin producir `.ko`.
- **workflow_dispatch / prepare**: descarga, verifica SHA y prepara fuentes con 21 cambios CZI4.
- **workflow_dispatch / kernel**: adicionalmente intenta compilar el kernel Samsung, recuperar toolchain Clang 19.0.1 r536225 y producir `Module.symvers`.
- **workflow_dispatch / kernel_and_probe**: además intenta crear el `a17_compat_probe.ko` con Kleaf, una prueba de ABI **que no es el RT Driver**.
- **kernel_and_rt_diag**: compila el target oficial Samsung
  `mgk_64_k612_kernel_aarch64.user` y el módulo nuevo `rt_a17_diag` en la
  misma invocación Bazel, con los flags del `build.sh` original. Evita
  compilar los módulos vendor y distribuir imágenes que este diagnóstico
  no necesita. No modifica la configuración de firma ni las protecciones.

El artefacto incluye `rt-diag-report.md` y `rt-diag-verification.json`,
`modinfo`, ELF, CRCs contra `Module.symvers`, SHA-256, `kernel.config` y
`kernel.release`. Una verificación fallida conserva el `.ko` como diagnóstico.

La salida se publica en artifacts de Actions bajo `a17-mediafire-build-report-<numero>` incluso si el trabajo falla.

**Advertencia:** MediaFire puede restringir las descargas automatizadas, exigir CAPTCHA o devolver HTML. Si ocurre, la descarga falla de forma explícita y hay que usar un origen de archivos que permita descargas sin interacción.

Las fuentes de Samsung no contienen el código fuente del RT Driver original y **no se ha verificado todavía un módulo funcional para el teléfono**. No se ejecutan flasheos, root, ni `insmod`.

### Auditoría de seguridad y trazabilidad (2026-10-17)

- El fallo #39011401897 se produjo en `BazelWorkspaceStatusAction`: el workspace extraído carece de Git y faltaba el `SOURCE_DATE_EPOCH` oficial para la segunda llamada a Bazel.
- Se volvió a generar `build.config` utilizando fuentes originales Samsung de la misma versión y se ejecutó `_setup_env.sh`: `KERNEL_VERSION=kernel-6.12`, `KERNEL_BUILD_VARIANT=user`, `SOURCE_DATE_EPOCH=0`. La prueba local `probe_preflight.sh` pasó sin compilar código. Durante el `source` se desactiva temporalmente `set -u`, puesto que Samsung utiliza variables opcionales sin definir.
- El comando Bazel del módulo se alinea con los flags del `kernel_device_modules-6.12/build.sh` oficial. La nueva etapa `preflight` debe completar el análisis `--nobuild` **antes** de considerar otra compilación del kernel.
- La ejecución fallida #39011401897 incluía **11** rutas de `Module.symvers`; antes se elegía una ruta temporal arbitraria. Ahora solo se permite la salida del target `mgk_64_k612_kernel_aarch64.user`, y se registra su SHA-256.
- La salida del probe debe localizarse como un único `a17_compat_probe.ko`, verificarse como ELF AArch64, examinar `modinfo`, y comparar estrictamente el primer token de `vermagic` con `6.12.38-android16-5-abA175FXXS7CZI4-4k`.
- **Pendiente**: resultado del preflight Bazel real, salida del probe compilado, KMI/CRC contrastados con el kernel instalado, estabilidad de carga, y el código funcional del RT Driver. No usar `insmod` ni afirmar compatibilidad hasta cerrar esos puntos.
