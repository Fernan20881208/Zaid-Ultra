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


## Compilación real mediante GitHub Actions (añadida)

El workflow `.github/workflows/a17-samsung-6.12-build.yml` **intenta ejecutar Samsung `kernel_device_modules-6.12/build.sh`**, no una simulación. El árbol fuente oficial y el overlay CZI4 ya se probaron localmente, pero **GitHub todavía no tiene los dos ZIP originales**, por lo que una compilación completa no se puede disparar automáticamente con los archivos adjuntos de ChatGPT.

### Habilitar la compilación en GitHub

1. En el repositorio, entrar a **Releases → Draft a new release**.
2. Crear el tag exacto **`a17-samsung-sources`** (por ejemplo, título "Samsung A17 source inputs").
3. Subir **ambos** archivos originales, sin renombrarlos ni extraerlos:
   - `SM-A175F_16_Opensource.zip` (~347 MB).
   - `SM-A175F_16_Opensource_A175FXXS7CZI4_A175NKSS7CZI2.zip` (~228 KB).
4. Publicar la Release **después** de adjuntar ambos ZIPs. Esto dispara el workflow automáticamente. También se puede ejecutar manualmente desde **Actions → Build Galaxy A17 kernel 6.12 (real sources) → Run workflow** seleccionando el modo `prepare`, `kernel` o `kernel_and_probe`.

Los ZIPs son código fuente abierto Samsung. Si el repo es público, los assets de esa Release también serán públicos. No agregar los ZIP a git, ya que GitHub rechaza archivos normales de más de 100 MB.

### Controles y artefactos

El workflow compara ambos SHA-256 contra archivos examinados, obtiene toolchains Android 16 / 6.12 con el manifiesto AOSP, exige Clang **r536225 (19.0.1)** e intenta el target Samsung `mgk_64_k612_kernel_aarch64.user`. En modo `kernel_and_probe`, intenta construir mediante Kleaf `a17_compat_probe.ko`.

**Importante:** conseguir una salida ELF ARM64 y `Module.symvers` no garantiza ABI o KMI idéntico al teléfono. La primera ejecución puede fallar por prebuilts, dependencias o discrepancias del entorno Samsung; los logs se adjuntan como artifact con `if: always()`. No se ejecuta `insmod` ni se modifica el teléfono.

La ejecución satisfactoria del workflow rápido de validación estática **no es** una compilación de kernel. El driver original de funciones de memoria y breakpoints sigue sin portarse porque no tenemos sus fuentes.
