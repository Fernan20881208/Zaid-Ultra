# A17 RT · Primera fase de ABI nueva

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

ESTADO: fuente nuevo preparado, no compilado ni probado sobre el A17.
No es el RT Driver original ni un reemplazo funcional para Zaid Hack.
El cliente y su protocolo de inicialización siguen sin verificar.
