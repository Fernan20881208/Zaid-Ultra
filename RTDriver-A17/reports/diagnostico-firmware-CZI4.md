# Diagnóstico recibido: Galaxy A17 SM-A175F / CZI4

Archivo aportado: `a17-buildinfo.txt` (16 octubre 2026). Datos del dispositivo; no son una compilación ni una prueba de carga.

| Parámetro | Resultado |
| --- | --- |
| Kernel completo | `6.12.38-android16-5-abA175FXXS7CZI4-4k` |
| Arquitectura | `aarch64` (diagnóstico anterior) |
| Root | `uid=0(root)`, KernelSU (`u:r:ksu:s0`) |
| Módulos | `CONFIG_MODULES=y`, `CONFIG_MODULE_UNLOAD=y` |
| Symvers / CRC | `CONFIG_MODVERSIONS=y` |
| CFI | `CONFIG_CFI_CLANG=y` |
| Firmas | `CONFIG_MODULE_SIG=y`, `CONFIG_MODULE_SIG_ALL=y`; `CONFIG_MODULE_SIG_FORCE` **no habilitado** |
| Exportaciones recortadas | `CONFIG_TRIM_UNUSED_KSYMS` no habilitado |
| Depuración | `CONFIG_DEBUG_INFO_BTF=y`, `CONFIG_KPROBES=y` |
| `/sys/kernel/kheaders.tar.xz` | Existe (~4.3 MB) |
| `/sys/kernel/btf/vmlinux` | Existe (~7.0 MB) |
| `/proc/config.gz` | Existe (~48 KB) |
| `Module.symvers` | No encontrado por el diagnóstico en ubicaciones de módulos consultadas |
| `kheaders.tar.xz` listado | No arrojó coincidencias en la prueba, sin demostrar ausencia de contenido |

## Interpretación

1. El driver antiguo `6.1.112-00034-g226e9f92a06c-dirty` no es un módulo compatible con esta compilación 6.12.38.
2. La configuración no obliga a firmar todos los módulos al momento de la carga (`CONFIG_MODULE_SIG_FORCE` desactivado), pero las políticas de runtime, Samsung y dependencias no están verificadas. `CONFIG_MODULE_SIG_ALL=y` implica firma durante el proceso de compilación de módulos.
3. `CONFIG_CFI_CLANG=y` exige un entorno de compilación compatible con la configuración exacta; coincidir en la cadena `uname -r` no basta.
4. Sin `Module.symvers` auténtico o un build completo equivalente de ese kernel **no se deben inventar CRC**. Los headers/BTF extraídos del dispositivo no sustituyen ese artefacto.
5. El ZIP fuente de Samsung correspondiente al firmware **A175FXXS7CZI4 no fue verificado como disponible** en el portal. La documentación oficial del firmware sí existe: https://doc.samsungmobile.com/SM-A175F/036466251223/spa-us.html
6. El código fuente original del RT Driver no está en el `.ko` adjunto: el ELF contiene símbolos y datos de depuración, pero faltan las unidades fuente para portar las funciones.
7. El log `dmesg` previamente aportado no incluye un mensaje detallado del cargador de módulos. No concluir que el problema sea exclusivamente `vermagic`, aunque sea una incompatibilidad evidente.

## Progreso técnico actual

- **Comprobado:** kernel exacto, firma no obligatoria por la configuración, CFI activo, headers y BTF, KernelSU y módulo antiguo incompatible.
- **Disponible en el repo:** código fuente C de un pequeño módulo de prueba `a17_compat_probe`, compilable sólo si se aporta un árbol construido para ese kernel exacto con `Module.symvers`. No hace lectura de memoria de procesos, no instala ni reinicia.
- **Pendiente:** fuente de Samsung y artefactos CRC/KMI reproducibles del build CZI4; auditoría y código fuente del RT Driver; compilación ARM64 contra artefactos completos y validación de carga por el usuario.
- **No afirmar:** que RT Driver ya fue portado, que el `.ko` nuevo carga, o que un `vermagic` modificado resuelve la incompatibilidad.

## Referencias

- Samsung Open Source: https://opensource.samsung.com/
- Samsung firmware SM-A175F (español): https://doc.samsungmobile.com/SM-A175F/036466251223/spa-us.html
- Kleaf `kernel_build` y `Module.symvers`: https://android.googlesource.com/kernel/build/+/refs/heads/master/kleaf/impl/kernel_build.bzl
- Android GKI fuentes genéricas (no reemplazan Samsung CZI4): https://android.googlesource.com/kernel/common/
