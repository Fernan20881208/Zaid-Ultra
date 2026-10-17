# Auditoría de integración de rt_a17_diag — 2026-10-17

Objetivo: SM-A175F, ARM64, Android 16, kernel
`6.12.38-android16-5-abA175FXXS7CZI4-4k`, Clang r536225.

Se inspeccionaron los archivos de RTDriver-A17, workflows, logs del build
cancelado 39054407329 y los dos ZIP oficiales aportados. Los SHA-256
coinciden con las verificaciones existentes. Se extrajo el árbol base,
aplicaron los 21 cambios CZI4 y generó build.config con el script Samsung.

## Errores resueltos

- `no_llseek` no está declarado en Linux Samsung 6.12.38. Se usa
  `nonseekable_open`, conservando permiso CAP_SYS_ADMIN y nodo 0600.
- La ruta anterior ejecutaba build.sh completo: después del kernel
  reconstruía miles de acciones de módulos vendor. El modo diagnóstico
  utiliza los dos targets necesarios con el entorno y flags oficiales.
- Selección de Module.symvers y del .ko: se rechazan copias de sandbox y
  resultados ambiguos; se conserva solo la salida canónica de Kleaf.
- La verificación ahora exige ELF64 little-endian ET_REL AArch64,
  nombre del módulo, dependencias, release generado por Kbuild y el
  vermagic completo calculado a partir de kernel.config. Registra firma,
  hashes y CRC de todos los símbolos importados, sin alterar el binario.
- El cliente de estado distingue ausencia, diagnóstico, acceso denegado
  y protocolo incompatible. Nunca convierte capacidades no verificadas
  en éxito funcional.

## Pruebas antes del build

Las verificaciones estáticas preexistentes se conservan. Se añadieron
pruebas del cliente real con syscalls controladas, identificación ELF,
vermagic según configuración, y rechazo de CRC incorrecto/ausente.
El collector rechaza dos resultados y copias de sandbox.

Se reprodujo el release con el `scripts/setlocalversion` original Samsung.
No se modifican ELF, vermagic, firmas ni configuración de seguridad.
El workspace_status local requiere los prebuilts AOSP; su análisis Bazel
completo se verifica en Actions, que recupera esos prebuilts.

## Flujo de compilación

Las solicitudes explícitas `build-requests/rt-diag-*.json` permiten iniciar
el workflow existente desde la conexión de GitHub, que no expone una
operación de workflow_dispatch. Cada solicitud es un commit independiente;
los commits ordinarios no disparan compilaciones del kernel.

El job reutiliza el workflow estático existente. Después prepara el árbol,
ejecuta `build --nobuild --config=stamp` para ambos módulos y solo entonces
compila `mgk_64_k612_kernel_aarch64.user` y `rt_a17_diag`.
El artefacto y el resumen contienen el resultado efectivo; este documento
no declara anticipadamente un .ko compilado.

## Pendientes de dispositivo y funciones originales

La igualdad de release y CRC contra Module.symvers del propio build no
prueba igualdad ABI/KMI con el kernel instalado. La aceptación de firmas
por el teléfono y la carga/estabilidad siguen sin probarse. No se carga
el módulo, no se reinicia ni se modifica ningún teléfono.

Los códigos y layouts DWARF originales siguen como documentación ABI.
Acceso a memoria de otros procesos, breakpoints y operaciones RT originales
no están implementados. El módulo y cliente se identifican como diagnóstico.
