# Moto g55 5G (taipei): investigación previa a la compilación

**Estado: configuración y Module.symvers recibidos; build DDK pendiente.**
No se ha compilado ni validado un módulo para este Motorola. Este registro no
añade un perfil compilado a `multikernel/profiles.json` ni solicita un build Samsung.

## Datos observados en las capturas

| Dato | Resultado |
| --- | --- |
| Modelo | moto g55 5G |
| `ro.product.device` | `taipei` |
| Incremental transcrito | `4720d-615a8a` |
| `uname -r` | `6.12.58-android16-6-gb0a9599c6c01-ab14987696-4k` |
| Root | `uid=0(root)`, contexto `u:r:magisk:s0` |
| `/sys/module/rt_a17` | No presente en esa consulta |
| `/sys/module/rt_a17_diag` | No presente en esa consulta |
| `/proc/version` | Lectura con root: Clang 19.0.1 Android 14043575, basado en r536225; LLD 19.0.1 |
| `uname -m` | `aarch64` |
| Timestamp del kernel | `Fri Mar 6 03:47:51 UTC 2026` |

La ausencia de ambos módulos indica que no estaban cargados con esos nombres;
no determina si existe un `.ko` descargado en el almacenamiento. El filtro de
`dmesg` sin resultados no demuestra compatibilidad ni identifica un fallo de
carga. La segunda captura y `kernel-taipei.config` aportan ya la arquitectura,
el compilador y la configuración completa.

## Fuente pública coincidente

La etiqueta oficial
[`android16-6.12-2025-12_r26`](https://android.googlesource.com/kernel/common/+/refs/tags/android16-6.12-2025-12_r26)
apunta a `b0a9599c6c01ec540e2252ca3a0e87b5b8c47c97` y referencia el
[build GKI 14987696, kernel_aarch64](https://ci.android.com/builds/submitted/14987696/kernel_aarch64/latest).
Coinciden el prefijo del commit y el número de build de `uname -r`. Esto
identifica una base pública concreta, pero no prueba identidad binaria con el
kernel instalado ni incorpora por sí solo los cambios vendor de Motorola.

Se descargaron y examinaron, fijados a ese commit:

- [`build.config.constants`](https://android.googlesource.com/kernel/common/+/b0a9599c6c01ec540e2252ca3a0e87b5b8c47c97/build.config.constants):
  `BRANCH=android16-6.12`, `KMI_GENERATION=6`, `CLANG_VERSION=r536225`,
  `RUSTC_VERSION=1.82.0`.
- [`BUILD.bazel`](https://android.googlesource.com/kernel/common/+/b0a9599c6c01ec540e2252ca3a0e87b5b8c47c97/BUILD.bazel):
  objetivo ARM64 con `gki_defconfig`, listas KMI y `trim_nonlisted_kmi=True`.
- [`gki_defconfig`](https://android.googlesource.com/kernel/common/+/b0a9599c6c01ec540e2252ca3a0e87b5b8c47c97/arch/arm64/configs/gki_defconfig):
  `CONFIG_LOCALVERSION="-4k"`, módulos, `MODVERSIONS`, CFI y firma habilitados
  en esa fuente. El archivo completo del teléfono se contrastó por separado.
- [`kernel/fork.c`](https://android.googlesource.com/kernel/common/+/b0a9599c6c01ec540e2252ca3a0e87b5b8c47c97/kernel/fork.c):
  contiene `EXPORT_SYMBOL_GPL(mm_access)`; no se ha clasificado este símbolo
  como ausente.

El A17 compilado usa KMI generación 5. Su módulo no constituye un binario
validado para este kernel de generación 6.

## Hallazgo de KMI

Se formó la unión de las 37 listas de
`aarch64_additional_kmi_symbol_lists` y la lista `base`: **38 archivos y 9856
nombres**. Se compararon los **60 nombres con CRC** del módulo RT v0.2 del
[build A17 39075095531](https://github.com/Fernan20881208/Zaid-Ultra/actions/runs/39075095531).
Faltan en esa unión:

- `register_user_hw_breakpoint`
- `modify_user_hw_breakpoint`
- `unregister_hw_breakpoint`

El `kernel_aarch64_Module.symvers` oficial se recuperó después: contiene **10041
exports** y confirma la ausencia de exactamente esos tres imports del módulo
A17 v0.2. No se debe enlazar el backend completo contra este GKI. La comparación
previa es de **nombres**, no una validación CRC del nuevo módulo; se comprobarán
sus CRC después de compilarlo.

[Evidencia JSON](moto-g55-taipei-kmi-audit.json) conserva los 60 nombres, los
archivos fuente y SHA-256 de las 38 listas. El nuevo perfil DDK compila
`rt_main.c` con `RT_A17_MEMORY_ONLY`, sin el backend de breakpoints. Anuncia
v0.3-memory, ABI 2, capacidades `0x7` y máscara de opcodes `0xe`. Los comandos
de breakpoints devuelven `EOPNOTSUPP`; INIT_KEY y GYRO siguen pendientes. El
perfil Samsung conserva el backend completo ABI 3 y sus pruebas.

## Correspondencia comprobada del build

La configuración recibida tiene 221042 bytes y SHA-256
`7e7f2c244ee2dd835d3689dcaea2a64c9f9028fe018134d67eca75433c4d12da`.
Es **idéntica byte por byte** al `kernel_aarch64_dot_config` del build 14987696.
Confirma `CONFIG_TRIM_UNUSED_KSYMS=y`, CFI, páginas de 4 KiB, MODVERSIONS básico
y extendido, `CONFIG_MODULE_SIG_PROTECT=y` y `CONFIG_MODULE_SIG_FORCE=n`.
Esto no prueba todavía que un módulo externo sea aceptado o funcione.

El `Module.symvers` oficial tiene SHA-256
`5ec414183b3ca4261c12f1837f03bc65bc89447f584226b07b6987f2b7220834`.
El manifest exacto tiene SHA-256
`111dab4e5b663f03210a77067cb9b9caac611232dc73e9ae4fec7b4e1df59ef9`:
fija Kleaf en `c99f48b81942c0eeac81e7fadd7a6e107b87a7f0` y los prebuilts
Clang en `77c95e8253c2c82ffd4fee92c0ad9f5ac1e8ca04`.

Se localizaron los artifacts mediante el endpoint público de listado del
frontend CI y se descargaron por `/latest/raw/<nombre-exacto>`. Las URLs
anteriores fallaban por endpoint/nombre, no por falta de autorización.

[`gki/14987696.json`](../gki/14987696.json) fija SHA-256, tamaño y MD5 publicado
de 31 archivos oficiales, incluidos el entorno de módulos preparado y los
headers DDK. Se reutilizará el kernel compilado por Google; se compilarán
solamente los módulos. `kernel.release` y los headers de versión proceden del
archivo oficial generado por Kbuild/Kleaf, sin fabricar versiones ni tocar ELF.

## Condiciones para una compilación verificable

1. Preparar los proyectos DDK fijados al manifest oficial y comprobar el
   compilador exacto contra `CONFIG_CC_VERSION_TEXT`.
2. Mantener excluido el backend de breakpoints en el perfil GKI y comprobar
   que las capacidades coincidan con las operaciones compiladas.
3. Pasar pruebas estáticas y análisis Kleaf `--nobuild` antes del build completo.
4. Reproducir el release mediante Kbuild/Kleaf y `localversion`; comparar el
   vermagic resultante, sin editar el ELF para aparentar compatibilidad.
5. Verificar ELF AArch64, imports/CRC con ese `Module.symvers`, firma y SHA-256;
   publicar el resultado en Actions sin declarar validación en dispositivo.

No se han iniciado compilaciones completas para esta solicitud. No se ha
cargado ningún módulo, flasheado el teléfono ni alterado firmas o protecciones.

## Validaciones previas al build de módulos

- Pruebas estáticas de Actions
  [39088596619](https://github.com/Fernan20881208/Zaid-Ultra/actions/runs/39088596619): satisfactorias.
- Reempaquetado y comprobación del build A17 anterior
  [39088596652](https://github.com/Fernan20881208/Zaid-Ultra/actions/runs/39088596652): satisfactorios;
  se toman las fuentes del commit del binario original y se conservan sus hashes.
- DDK local: todos los artifacts y revisiones fijados se recuperaron, y Clang
  coincide con el identificador completo Android 14043575 de la configuración.
- Kleaf `build --nobuild --config=stamp //rt:rt_a17_diag //rt:rt_a17`:
  **2 targets analizados, 68 paquetes, 19103 targets configurados, 0 acciones**.
  Se usó el modo Bazel `--batch` por las limitaciones del servidor de procesos
  de este entorno local; no se ejecutó una compilación.

El análisis detectó y permitió corregir dos errores antes del build: incluir
el subárbol oficial `kleaf` en el checkout selectivo de Clang y usar el paquete
real `@gki_prebuilts//kernel_aarch64:kernel_aarch64`, generado por el DDK.
