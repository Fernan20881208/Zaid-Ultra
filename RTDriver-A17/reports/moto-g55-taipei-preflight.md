# Moto g55 5G (taipei): investigación previa a la compilación

**Estado: pendiente de configuración y Module.symvers exactos.** No se ha
compilado ni validado un módulo para este Motorola. Este registro no añade un
perfil compilado a `multikernel/profiles.json` ni solicita una compilación Samsung.

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
| `/proc/version` | Intento sin root: `Permission denied` |

La ausencia de ambos módulos indica que no estaban cargados con esos nombres;
no determina si existe un `.ko` descargado en el almacenamiento. El filtro de
`dmesg` sin resultados no demuestra compatibilidad ni identifica un fallo de
carga. La captura no aporta `uname -m` ni la configuración completa.

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
  en esa fuente. No es una extracción de la configuración del teléfono.
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

Con el recorte KMI configurado en el build oficial, esto sugiere que el backend
actual de breakpoints puede no enlazar como módulo externo. Es una inferencia
de las listas fuente: hace falta el `Module.symvers` del build correspondiente
para confirmar los exports efectivos y sus CRC. La comparación realizada es de
**nombres**, no de CRC entre kernels; que los otros nombres aparezcan en KMI no
prueba que el port compile, cargue o funcione.

[Evidencia JSON](moto-g55-taipei-kmi-audit.json) conserva los 60 nombres, los
archivos fuente y SHA-256 de las 38 listas. No se han eliminado las dependencias
de breakpoints ni modificado las capacidades del módulo existente. Si ese
backend no puede usarse, deberá implementarse una variante que anuncie con
precisión sus capacidades y rechace las operaciones pendientes.

## Datos necesarios del teléfono

Estas consultas requieren ejecución manual por el propietario en Termux;
no cargan módulos ni cambian políticas:

```sh
su -c 'cat /proc/version; uname -m'
su -c 'zcat /proc/config.gz' > "$HOME/kernel-taipei.config"
```

Enviar la salida de la primera consulta y el archivo `kernel-taipei.config`.
Si `zcat` falla o el archivo queda vacío, conservar el error y no tratarlo como
una configuración válida. La configuración puede contener rutas locales de
compilación; revisar el archivo antes de compartirlo. No hacen falta IMEI,
número telefónico, cuentas ni otros identificadores personales.

Después se necesita recuperar el `Module.symvers` y la configuración completa
del build correspondiente, o reproducirlos mediante sus fuentes y manifest
oficiales. Los intentos con URLs directas de artifacts no recuperaron todavía
un `Module.symvers`: el endpoint probado respondió 501 y las rutas alternativas
404. Esto no demuestra que el artefacto no sea público.

## Condiciones para una compilación verificable

1. Confirmar arquitectura, configuración, toolchain, KMI y exports efectivos.
2. Resolver el backend de breakpoints según las APIs públicas disponibles,
   conservando la ABI y publicando las capacidades reales.
3. Pasar pruebas estáticas y análisis Kleaf `--nobuild` antes del build completo.
4. Reproducir el release mediante Kbuild/Kleaf y `localversion`; comparar el
   vermagic resultante, sin editar el ELF para aparentar compatibilidad.
5. Verificar ELF AArch64, imports/CRC con ese `Module.symvers`, firma y SHA-256;
   publicar el resultado en Actions sin declarar validación en dispositivo.

No se han iniciado compilaciones completas para esta solicitud. No se ha
cargado ningún módulo, flasheado el teléfono ni alterado firmas o protecciones.
