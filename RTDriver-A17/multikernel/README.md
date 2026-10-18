# RT para kernels ARM64 6.12: kit por perfil

No existe aquí un único `.ko` o una imagen de arranque para todos los kernels
6.12. Este kit conserva el mismo código fuente del port y compila un módulo
por kernel/configuración. Un nombre que empieza por `6.12` no prueba KMI,
CRC, arquitectura, aceptación de firma ni compatibilidad del dispositivo.

El único perfil binario incluido inicialmente es **Samsung Galaxy A17
SM-A175F, CZI4**, kernel `6.12.38-android16-5-abA175FXXS7CZI4-4k`. Su módulo
`rt_a17.ko` v0.2 fue compilado en Actions #39075095531; es ABI 3, parcial,
sin firma y no probado sobre el teléfono. El paquete verifica otra vez su
ELF AArch64, vermagic, imports, tablas CRC básicas/extendidas y hashes antes
de publicarlo. El diagnóstico sigue siendo otro módulo con capacidades cero.

## Comprobación desde Android

Extrae el ZIP y, desde su carpeta, ejecuta:

```sh
su -c 'sh /sdcard/Download/RT-6.12-kit/RTDriver-A17/multikernel/check_kernel.sh'
```

Ajusta la carpeta al lugar donde lo extraíste. El comprobador es de lectura:
no carga módulos, no cambia firmas, no reinicia ni altera el dispositivo.
Un perfil con release/arquitectura coincidentes y hashes correctos devuelve
`candidate_device_validation_required` (salida 6), nunca "compatible probado".
Otro kernel 6.12 devuelve `build_required_for_exact_kernel` (salida 7). Un
modelo diferente, aunque coincida el release, exige validación aparte.
Los SHA comprueban consistencia del paquete; no sustituyen una firma de confianza.

## Compilar para otro kernel 6.12

Hace falta el árbol fuente exacto del dispositivo, la salida de una compilación
del kernel con su `.config`, headers generados y `Module.symvers`, y el mismo
toolchain. `modules_prepare` por sí solo no genera `Module.symvers`.
El kit no modifica `CONFIG_LOCALVERSION`, las opciones de firma ni el ELF.
Las diferencias de API o símbolos no exportados producirán un fallo real.

```sh
python3 RTDriver-A17/multikernel/rt612.py build \
  --kernel-source /ruta/kernel-6.12 \
  --kernel-out /ruta/salida-oficial \
  --check-only

python3 RTDriver-A17/multikernel/rt612.py build \
  --kernel-source /ruta/kernel-6.12 \
  --kernel-out /ruta/salida-oficial \
  --llvm /ruta/clang-original/bin/ \
  --output /ruta/nueva/resultado
```

El segundo comando compila solamente los dos módulos mediante Kbuild, guarda
el log y comprueba binarios/imports/CRCs. No instala ni carga módulos. Si el
árbol oficial exige Kleaf, hay que integrar los targets `kernel_module` en su
grafo como en el workflow Samsung existente; el comando Kbuild genérico no
reemplaza los requisitos del fabricante. RANDSTRUCT requiere un perfil y
semilla generada propios y se rechaza por ahora.

No se aportan árboles/configuraciones para otros dispositivos. Este es soporte
para **construir y validar nuevos perfiles**, no una afirmación de que el código
ya haya compilado o funcionado en todas las variantes 6.12. Para añadir un
perfil se necesita su build real, release, configuración, CRCs y artefacto
con hashes; el prefijo del kernel nunca selecciona un binario de otro perfil.

## Alcance del port

ABI 3 anuncia READ_MEM, WRITE_MEM y MODULE_BASE acotados y breakpoints de
hardware de un solo disparo con cola por descriptor. INIT_KEY y GYRO_CONFIG
siguen sin implementar. Se mantienen permisos CAP_SYS_ADMIN, ptrace/LSM y
CAP_SYS_PTRACE para breakpoints. Las máscaras indican código implementado;
`original_rt_fully_verified` permanece en `false`. No se integró en el APK.

## Trazabilidad del paquete

`profiles.json` fija el run, artefacto y hashes de cada perfil. El workflow de
este kit descarga el ZIP inmutable del build A17 y comprueba su SHA completo.
El paquete entrega binarios sin modificarlos, `Module.symvers`, configuración,
evidencia de modinfo/CRC y las herramientas manuales Android. Produce un
`bundle-verification.json` nuevo; omite el Markdown heredado de ABI 2 del
artefacto original y conserva los datos correctos del binario v0.2.

El empaquetado recibe `bundle --source-root /ruta/al/RTDriver-A17` del commit
que produjo el binario. Comprueba los hashes fuente de ese perfil antes de
copiarlos; un cambio posterior en el port no se presenta como el código del
artefacto antiguo. El workflow obtiene ese snapshot en `source-at-a17-build`.
El perfil GKI `taipei` tiene su [flujo DDK separado](https://github.com/Fernan20881208/Zaid-Ultra/tree/main/RTDriver-A17/gki);
no se incorpora a este índice de perfiles compilados hasta verificar su build.

Documentación de Kbuild: https://docs.kernel.org/kbuild/modules.html
