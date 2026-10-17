# Zaid Kernel Checker

**App Android independiente** alojada dentro de Zaid-Ultra únicamente para compartir el repositorio.

- **Nombre:** Zaid Kernel Checker
- **Package ID:** com.zaid.kernelchecker
- **Android:** 8.0+ (API 26)
- **Root:** opcional para datos básicos; necesario para diagnosticar completamente módulos restringidos
- **Funciones:** kernel/arquitectura, SELinux, UID root, /proc/modules, /sys/module, /dev/BNYYYB, /dev/lpm, /proc/devices, inspección local de ELF .ko ARM64 y vermagic, copiar/compartir reporte
- **Seguridad:** exclusivamente lectura. NO ejecuta check_driver, no carga drivers ni hace ioctls desconocidos. No usa Internet ni añade servicios.

## APK

Abre la pestaña **Actions** de Zaid-Ultra y selecciona **Zaid Kernel Checker APK**. Al terminar el build descarga el artefacto **Zaid-Kernel-Checker-APK** y extrae **app-debug.apk**.

El workflow vive en /.github/workflows/zaid-kernel-checker.yml pero todo el código/Gradle está aislado en /Zaid-Kernel-Checker. Las compilaciones de los demás proyectos no se modifican.

## Qué significa "detectado"

Encontrar un módulo en /proc/modules o /sys/module solo confirma presencia en el kernel. Encontrar un nodo /dev confirma una interfaz registrada, no que los comandos ioctl funcionen. Un vermagic coincidente no garantiza símbolos/CRC/KMI, compatibilidad ni estabilidad.

Se muestran los módulos conocidos como 5.10_A12, BNYYYB y otros con nombre relacionado con Zaid, Krishna, GT o lpm. También se admite un nombre exacto indicado por el usuario.

## Fuente

Código Java 17 / Android SDK 35, Gradle 8.10.2 / AGP 8.7.3. Ejecutar tests locales con javac, y build Android en GitHub Actions. La firma de APK debug no es estable para distribución.
