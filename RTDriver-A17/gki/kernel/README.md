# Kernel Moto g55 taipei con RT integrado

Este perfil recompila la imagen de kernel ARM64 desde `kernel/common`
`b0a9599c6c01ec540e2252ca3a0e87b5b8c47c97`, baseline oficial 14987696,
con sus herramientas fijadas en el manifest publicado y Clang r536225.
No es una imagen universal para todas las versiones 6.12 ni otro repositorio.

Kconfig habilita `CONFIG_RT_A17=y`. Kbuild enlaza `rt_main.c` y
`rt_breakpoints.c` dentro de vmlinux; así las tres APIs normales de breakpoints
pueden resolverse sin agregarlas a las exportaciones KMI. Se conserva el
Makefile existente para los módulos externos y los perfiles Samsung/Moto DDK.

El backend ABI 3 implementa READ_MEM, WRITE_MEM, MODULE_BASE, BP_CONTROL,
BP_GET_HITS y la consulta BP_STATUS, con breakpoints de un solo disparo.
INIT_KEY y GYRO_CONFIG devuelven EOPNOTSUPP; el ELF original analizado tampoco
los despachaba. No se inventan esas operaciones ni se declara verificado el APK.
INFO anuncia capacidades 0x3f y máscara 0x6e; `functional_verified=false`
permanece en las herramientas hasta disponer de resultados del dispositivo.

El workflow `rt-taipei-kernel.yml` exige las pruebas existentes, `--nobuild`
y una configuración generada antes de compilar la imagen. Solo permite
cambiar CONFIG_RT_A17 y CONFIG_LOCALVERSION respecto a la configuración
recibida del teléfono. El sufijo `-4k-rt-taipei` y el SCM stamp oficial
identifican el kernel personalizado; no se edita ningún ELF o vermagic.

La verificación exige Image ARM64, vmlinux AArch64, RT en modules.builtin,
su initcall y funciones enlazadas, igualdad de todas las exportaciones/CRC
con el Module.symvers original y los módulos GKI correspondientes firmados.
Se publican Image, Image.gz, Image.lz4, vmlinux, Module.symvers, configuración,
módulos GKI, certificado público, hashes e informe, incluso como diagnóstico
cuando falle una comprobación posterior. No se publica la clave privada.

La comprobación posterior `rt-taipei-kernel-verify.yml` descarga el artefacto
de esa ejecución y contrasta su commit de origen. Permite corregir/verificar
los controles del informe sin repetir la compilación del kernel. vmlinux es
ET_DYN cuando CONFIG_RELOCATABLE=y, como en el binario oficial; modules.builtin
usa el prefijo `kernel/` generado por scripts/Makefile.vmlinux_o.

La clave privada de Google no está disponible. Kbuild genera una nueva clave
para los módulos GKI de esta compilación. Se preserva MODULE_SIG_PROTECT;
los módulos protegidos stock firmados por Google no se dan por aceptados
por el kernel personalizado. El paquete incluye sus módulos recompilados.
La compatibilidad de drivers vendor, el arranque y los breakpoints físicos
requieren pruebas reales. No se modifican el teléfono, SELinux ni sus firmas.

Para preparar y verificar sin compilar la imagen:

```sh
export GKI_KERNEL_WORKSPACE=/ruta/nueva/kernel-taipei
export GKI_KERNEL_REPORT=/ruta/informe
bash RTDriver-A17/scripts/ci_build_gki_kernel.sh preflight
```

Una solicitud nueva `RTDriver-A17/kernel-requests/taipei-*.json` que solo
añada ese archivo, apunte al commit padre y declare `mode=kernel`,
`rt_integration=builtin` y `build_id=14987696` solicita una compilación.
Las ediciones ordinarias no disparan compilaciones completas.

La imagen raw no tiene firma AVB y no es un boot.img stock. Todavía no se
proporcionó el boot.img original del firmware `4720d-615a8a` para comprobar
el empaquetado específico de este Moto. Ningún script flashea o carga nada.
