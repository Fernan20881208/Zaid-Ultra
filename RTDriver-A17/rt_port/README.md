# A17 RT · port ABI parcial

`rt_a17_diag` sigue siendo un módulo de diagnóstico con capacidades cero.
El módulo separado `rt_a17` v0.2 (ABI 3) compila operaciones de memoria
acotadas, búsqueda de base de mapeo y breakpoints de hardware de un disparo.
La compilación y los CRC contra el kernel generado pasaron en Actions
**#39075095531**. Esto no demuestra KMI del teléfono, aceptación de firma,
ejecución en SM-A175F ni integración con Zaid Hack.

[Informe corregido, SHA-256, modinfo y artefacto descargable](../reports/rt-port-build-39075095531.md) ·
[Implementación y límites](runtime/README.md).

Origen: RTdriver_extracted_arm64.ko.
SHA-256: 0df5b0e7710af752ab21306dcd1b4be3b40598c977ccd1de6b95fd43d0c8ba72.

Datos recuperados mediante DWARF del archivo ELF ARM64:
- OPERATIONS (DIE 0xeeb0): 0x800 a 0x806.
- _COPY_MEMORY (DIE 0x124df): tamaño 32, offsets 0/8/16/24.
- _MODULE_BASE (DIE 0x12523): tamaño 24, offsets 0/8/16.
- _HW_BREAKPOINT_CTL (DIE 0x4f): tamaño 32, offsets 0/8/16/20/24.
- _HW_BREAKPOINT_GET_HITS_CTL (DIE 0xe8): tamaño 16, offsets 0/8.

Archivos nuevos:
- rt_abi.h: formatos de las estructuras y números de operación.
- rt_a17_diag.c: pequeño dispositivo diagnóstico /dev/rt_a17_diag.
- Makefile y BUILD.bazel: configuración específica Samsung Kleaf.
- tests/test_abi.c: validación de tamaños y offsets.
- tests/test_makefile.sh: valida Kbuild sin realizar una compilación completa.

Este módulo de diagnóstico solo permite leer una estructura de capacidades
a través del comando 0x900. Las operaciones originales 0x800 a 0x806
devuelven explícitamente -EOPNOTSUPP.

ESTADO: v0.2 compilado realmente en Actions #39058053061. ELF ET_REL AArch64,
vermagic completo exacto y 8 CRC coincidentes con el kernel del build; sin firma.
[Informe y descarga del artefacto](../reports/rt-diag-build-39058053061.md).
No probado sobre el teléfono; ABI/KMI del dispositivo sin verificar.
No es el RT Driver original ni un reemplazo funcional para Zaid Hack.
El cliente y su protocolo de inicialización siguen sin verificar.

## Integración en Samsung Kleaf

El flujo `RTDriver-A17/scripts/ci_build_a17.sh` ya incluye el objetivo
`//kernel_device_modules-6.12/rt_a17_diag:rt_a17_diag` en el análisis
`preflight` (`--nobuild`).

Para generar el nuevo módulo de diagnóstico **cuando el preflight sea exitoso**,
existe la opción manual `kernel_and_rt_diag` en el workflow
`.github/workflows/a17-samsung-6.12-build.yml`.

La opción reconstruye el kernel con las fuentes oficiales Samsung,
compila el objetivo del módulo, busca exclusivamente el resultado
canónico de Bazel y registra ELF/ARM64, `modinfo`, CRCs,
`vermagic`, SHA-256 y datos de firma. Publica reportes en
`a17-ci-results/` incluso si algún chequeo falla.

No ejecuta `insmod`, no instala módulos al inicio y no escribe en
el dispositivo Android. Una cadena de versión que coincida no garantiza
firma o compatibilidad con los símbolos reales del kernel instalado.

## Integración con la aplicación

`tools/rt_a17_status.c` consulta el comando nuevo `0x900`.
El controlador devuelve `original_ioctls_implemented=0` y
`capabilities=0`; el cliente marca la situación como
**solo diagnóstico** con código de salida 4. Emite JSON para que una app
pueda consumir el resultado sin interpretar mensajes de consola.

| `state` | Salida | Interpretación |
| --- | --- | --- |
| `absent` | 2 | Nodo ausente o dispositivo ya retirado |
| `diagnostic` | 4 | ABI 1 reconocido, capacidades y operaciones originales en cero |
| `compatibility_error` | 3 | IOCTL no reconocido, ABI distinta o campos inconsistentes |
| `inaccessible` | 5 | Acceso denegado u otro fallo al abrir el nodo; no implica ausencia |
| `functional_verified` | Reservado | No lo emite esta versión; requiere implementación y pruebas funcionales independientes |

`functional_verified` es siempre `false` en el JSON de este cliente.
Ni un nodo existente ni un campo de capacidades distinto de cero bastan para
declarar que el RT Driver funciona. Las operaciones DWARF documentadas
permanecen sin implementar y devuelven `-EOPNOTSUPP`.

El cliente fuente reconoce ABI 1 (diagnóstico), ABI 2 (memoria) y ABI 3
(memoria y breakpoints), y distingue estados sin declarar funciones verificadas.
No se integró dentro del APK: los archivos disponibles contienen APK/DEX y
assets binarios, no el código fuente mantenible de la aplicación.
