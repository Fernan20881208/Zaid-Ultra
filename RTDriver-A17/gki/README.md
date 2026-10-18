# RT para GKI 14987696 / Moto g55 taipei

El perfil de [kernel con RT integrado](kernel/README.md) recompila la imagen
con el backend de memoria y breakpoints dentro de vmlinux. Es una ruta distinta
del DDK externo descrito aquí; mantiene pendiente la validación en el teléfono.

**Build verificado en Actions #39088889251:** `rt_a17.ko` v0.3-memory
(45 CRC) y `rt_a17_diag.ko` (9 CRC), ambos ELF AArch64, vermagic exacto y sin
firma. [Informe y descarga](../reports/moto-g55-build-39088889251.md) ·
[Perfil compilado](taipei-14987696-profile.json).

Este perfil usa el kernel oficial precompilado `kernel_aarch64` del build
14987696 y su DDK preparado. La configuración recibida del teléfono coincide
exactamente con ese build. No genera una imagen de kernel ni un binario para
todos los 6.12.

El workflow `rt-gki-6.12-build.yml` se inicia mediante una solicitud explícita
`build-requests/gki-*.json` o manualmente. Exige pruebas estáticas y análisis
Kleaf `--nobuild` antes de compilar los módulos. El manifest y todos los
artefactos de entrada están fijados por commit o SHA-256 en `14987696.json`.

`rt_a17_diag.ko` conserva ABI 1 de diagnóstico. `rt_a17.ko` v0.3-memory usa ABI
2, capacidades 0x7 y máscara 0xe: READ_MEM, WRITE_MEM y MODULE_BASE. Los
breakpoints no están disponibles porque sus tres APIs no están exportadas en
este build. Esos comandos, INIT_KEY y GYRO devuelven EOPNOTSUPP. Se conservan
las comprobaciones de permisos y ptrace/LSM; no hay búsqueda de APIs privadas.

Los módulos conservan el nombre histórico para la interfaz existente. No se
debe usar el binario A17 CZI4 en el Motorola ni presentar esta variante como
el RT original completo. La aplicación debe consultar INFO y sus capacidades.

El DDK oficial configura la compilación de módulos externos sin la clave de
firma privada de Google. No cambia las opciones o protecciones del kernel
instalado. El informe registra la firma real de los binarios y verifica sus
imports en la lista GKI permitida, vermagic y CRC contra el Module.symvers
oficial. La carga y ejecución en el teléfono permanecen pendientes.

Preparación local en Linux (solo descarga y configuración):

```sh
python3 RTDriver-A17/scripts/prepare_gki_ddk.py \
  --workspace /ruta/nueva/ddk --cache /ruta/cache-gki
cd /ruta/nueva/ddk
tools/bazel build --nobuild --config=stamp //rt:rt_a17_diag //rt:rt_a17
```

Ningún script carga módulos, cambia SELinux, modifica firmas, flashea o reinicia
el teléfono. Los binarios fallidos se mantienen como evidencia de diagnóstico.
