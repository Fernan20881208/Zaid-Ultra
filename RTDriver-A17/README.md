# Galaxy A17 RT Driver — port seguro, fase 2

**Objetivo:** SM-A175F, Android 16, `6.12.38-android16-5-abA175FXXS7CZI4-4k`.

**Estado:** todavía NO es el RT Driver. Este directorio contiene un *módulo de prueba de compatibilidad*, no el controlador original ni un módulo probado en el teléfono.

## Datos verificados con el diagnóstico real del teléfono

- Kernel: `6.12.38-android16-5-abA175FXXS7CZI4-4k`
- Arquitectura: `aarch64`, Android Clang 19.0.1.
- Root: KernelSU, `uid=0`, SELinux `u:r:ksu:s0`.
- `CONFIG_MODULES=y`, `CONFIG_MODULE_UNLOAD=y`, `CONFIG_MODVERSIONS=y`.
- `CONFIG_MODULE_SIG=y`: no se conoce todavía `CONFIG_MODULE_SIG_FORCE` ni la política completa de firmas.
- `CONFIG_KPROBES=y`, `CONFIG_DEBUG_INFO_BTF=y`.
- `/sys/kernel/btf/vmlinux` y `/sys/kernel/kheaders.tar.xz` presentes.
- Los logs compartidos NO muestran una razón detallada del rechazo del RT Driver: las líneas capturadas son `connlog`, no el error del cargador.

La versión original del módulo extraído es `6.1.112-00034-g226e9f92a06c-dirty`: **incompatible con 6.12.38**. No basta con editar `vermagic`, CRC, nombre, o empaquetar el archivo.

La actualización A175FXXS7CZI4 está documentada por Samsung:
https://doc.samsungmobile.com/SM-A175F/036466251223/eng.html

## Siguiente paso: símbolos auténticos

Linux Kbuild necesita un árbol **preparado para el kernel exacto** y el `Module.symvers` con CRC reales (no inventados). `modules_prepare` por sí solo NO genera `Module.symvers` cuando `CONFIG_MODVERSIONS=y`. La presencia de BTF y kheaders en el teléfono tampoco reemplaza estos requisitos.

1. Buscar el paquete de fuentes exacto en https://opensource.samsung.com (SM-A175F/A175FXXS7CZI4).
2. Identificar el build Kleaf utilizado por ese firmware y sus artefactos KMI/Module.symvers.
3. Obtener fuentes del RT Driver original o desarrollar una implementación propia con especificación clara. Solo se dispone del binario, cuyos símbolos referencian funciones de kprobes y memoria de procesos.
4. Validar primero el módulo mínimo de esta carpeta contra un árbol Samsung exacto, sin cargarlo automáticamente.
5. Auditar el driver original antes de portar cualquier manejo de memoria, ioctl o interrupciones.

## Scripts

Recolectar información adicional (solo lectura; se escribe un TXT en Download):

```sh
su -c 'sh /sdcard/Download/collect_a17_buildinfo.sh'
```

Compilar **solo el probe** cuando se cuente con el árbol Samsung exacto, preparado y con `Module.symvers`:

```bash
bash RTDriver-A17/scripts/build_probe.sh /ruta/arbol-kernel-preparado
```

**No hay instalador, no se ejecuta `insmod`, no se modifica el kernel, no se salta SELinux o la firma.**
