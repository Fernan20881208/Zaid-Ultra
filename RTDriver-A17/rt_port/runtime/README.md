# RT A17: primer port parcial

**Compilado en Actions #39071647279:** `rt_a17.ko` de 26960 bytes, ET_REL AArch64,
vermagic completo exacto y 45 CRC coincidentes con su kernel, sin firma.
[Informe, hashes, modinfo y descarga](../../reports/rt-port-build-39071647279.md).

`rt_a17.c` implementa tres operaciones del ABI recuperado. Es una implementación
nueva, separada del módulo de diagnóstico `rt_a17_diag.c`, que conserva su ABI 1.
No se ha ejecutado en el teléfono ni se ha verificado su uso con el APK original.

| Comando | Implementación en `rt_a17` |
| --- | --- |
| `0x801` READ_MEM | Lectura de memoria de datos, máximo 65536 bytes |
| `0x802` WRITE_MEM | Escritura de memoria de datos, máximo 65536 bytes |
| `0x803` MODULE_BASE | Primera dirección de un mapeo cuyo archivo tiene exactamente ese nombre |
| `0x800` INIT_KEY | `-EOPNOTSUPP`; inicialización privada pendiente |
| `0x804` GYRO_CONFIG | `-EOPNOTSUPP` |
| `0x805` BP_GET_HITS | `-EOPNOTSUPP` |
| `0x806` BP_CONTROL | `-EOPNOTSUPP` |
| `0x900` INFO | ABI 2, capacidades `0x7`, máscara de operaciones originales `0xe` |

`MODULE_BASE` devuelve el comienzo del primer mapeo del archivo; no calcula un
load bias ELF general. Una búsqueda sin coincidencias devuelve éxito con
`base=0`, como el binario examinado. READ y WRITE devuelven 0 al completar,
y errno negativo al fallar. Los tamaños, offsets y opcodes de `rt_abi.h`
se conservan. Los campos de relleno del ABI original se ignoran.

El nodo es `/dev/rt_a17`, modo `0600`. Abrirlo y cada ioctl requieren
`CAP_SYS_ADMIN`. El acceso al proceso requiere además `mm_access` con
`PTRACE_MODE_ATTACH_REALCREDS`: se conservan las verificaciones ptrace y LSM.
El módulo conserva una referencia al mismo `mm` autorizado para evitar una
carrera con `exec`. No fuerza accesos a páginas sin permisos, no accede a
memoria física ni busca símbolos privados. Rechaza escrituras en mapeos
ejecutables, mapeos IO/PFN, huecos y punteros etiquetados de transferencia.

Antes de escribir copia todo el buffer del usuario y fija todas las páginas
del rango acotado. Un fallo de validación, copia de origen o fijación parcial
no modifica las páginas de destino. No ofrece transacciones frente a otras
escrituras concurrentes del proceso, ni una instantánea consistente de memoria.
La copia final de una lectura al buffer del usuario puede ser parcial si ese
buffer falla, y entonces devuelve `EFAULT`.

`../tools/rt_a17_client.[ch]` es el adaptador fuente para una integración futura.
`rt_a17_port_status` consulta primero el port y después el diagnóstico cuando
el nodo del port está ausente. Emite JSON:

| Estado | Salida |
| --- | --- |
| `absent` | 2 |
| `compatibility_error` | 3 |
| `diagnostic` | 4 |
| `inaccessible` | 5 |
| `partial_unverified` | 6 |

`functional_verified` permanece en `false`. Una máscara de capacidades indica
código implementado, no certifica pruebas sobre un dispositivo. El cliente ABI 1
original permanece disponible y no acepta capacidades distintas de cero.

`rt_a17_selftest --run-owned-child` es una prueba **manual** para Android ARM64.
Solo abre un nodo ya disponible y trabaja con un hijo creado por la propia
herramienta y archivos temporales propios en `/data/local/tmp`. Comprueba
transferencias que cruzan páginas, escritura observada por el hijo,
permisos de mapeos, errores de punteros, búsqueda de módulos y opcodes pendientes.
No carga ni instala el módulo. Un resultado positivo tiene ese alcance limitado;
no verifica breakpoints ni toda la funcionalidad del RT original.

El workflow dispone de `kernel_and_rt_port`: conserva el diagnóstico, añade
el target `//kernel_device_modules-6.12/rt_a17_diag/runtime:rt_a17`, analiza
los tres targets con `--nobuild` y solo después compila. Los artefactos incluyen
ambos módulos, el Module.symvers del kernel, informes independientes y las
herramientas Android estáticas. No cambia las opciones de firma o del dispositivo.

Faltan pruebas sobre SM-A175F, KMI frente al kernel instalado, aceptación de
firma, breakpoints y el código fuente del cliente original para integrar su
selección de nodo/capacidades. No se ha modificado ni vuelto a firmar el APK.
