# Recuperación del ABI y primer tramo del port RT

Se inspeccionó estáticamente el ELF ARM64 contenido, sin codificación base64,
a partir del offset 2942 del archivo `RT/6.1XBY_SANDIP.sh` de `Kernel-Drivers.7z`.
No se ejecutó ese script ni el módulo. El ELF termina al final de su tabla de
secciones: 860632 bytes, SHA-256
`0df5b0e7710af752ab21306dcd1b4be3b40598c977ccd1de6b95fd43d0c8ba72`.
Coincide exactamente con el binario documentado previamente en este repositorio.

DWARF confirma `OPERATIONS` en DIE `0xeeb0`, `_COPY_MEMORY` en `0x124df`,
`_MODULE_BASE` en `0x12523`, `_HW_BREAKPOINT_CTL` en `0x4f` y
`_HW_BREAKPOINT_GET_HITS_CTL` en `0xe8`. No se cambian esos layouts.

La descompilación no se trata como fuente original. El análisis de instrucciones
AArch64 y relocaciones de `dispatch_ioctl` (sección `.text`, inicio `0x2e0`)
permite comprobar:

- La tabla de saltos despacha READ a `0x388`, WRITE a `0x6d4` y MODULE_BASE a `0x464`.
- READ llama `read_process_memory` en `0x458`; WRITE llama `write_process_memory`
  en `0x7a4`. Un retorno no nulo lleva a `0xab0`, que devuelve 0 al cliente.
- MODULE_BASE recorre VMAs con `find_vma`, obtiene la ruta con `d_path` (`0x694`),
  compara el último componente con `strcmp` (`0x6b8`) y entrega `vm_start`
  en `0x9d8`. Copia la estructura de 24 bytes al usuario en `0xa60`.
  La ausencia de un mapeo devuelve `base=0`.
- INIT_KEY cae fuera del rango de la tabla y devuelve `EINVAL` en este binario.
  GYRO_CONFIG también conserva ese retorno. No se inventa una autenticación
  equivalente en el port: ambas operaciones devuelven `EOPNOTSUPP`.
- Existen manejadores de breakpoints y sus layouts, pero quedan pendientes.
  Su existencia en el ELF no demuestra compatibilidad con Linux 6.12 ni con el A17.

El módulo nuevo sustituye el acceso físico y los offsets privados del binario
por `mm_access`, `pin_user_pages_remote`, mapeos locales y VMAs del kernel.
Se verificó que esas APIs y las de liberación están exportadas por el
Module.symvers del build Samsung #39058053061. La compilación nueva debe
resolver sus CRC con el Module.symvers de su propio kernel, no con ese listado
histórico.

Las pruebas de host ejecutan el C de producción con APIs del kernel simuladas
para inyectar fallos y verificar liberación de referencias, permisos, buffers,
pin parcial y protocolo. No se presentan como una prueba del kernel ARM64.
La herramienta manual Android ensaya memoria de un hijo propio cuando el
propietario decida ejecutarla sobre un módulo ya cargado por sus medios.

El APK y su ejecutable nativo se examinaron como material binario. No se dispone
del proyecto fuente del cliente original y no se ha reconstruido ni modificado
su inicialización privada. Se entrega un adaptador C fuente y consulta de
capacidades para facilitar esa integración sin atribuirle resultados inexistentes.
