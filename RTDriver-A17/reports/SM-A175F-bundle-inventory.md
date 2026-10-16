# Inventario confirmado: SM-A175F.zip

Archivo enviado por el usuario: `SM-A175F.zip`
- SHA-256: `8d05b158f0b393ef0f62771a5dc29f96298813a793cdcb0bd6acf442aa985b6a`
- Tamaño aproximado: 477 KiB.
- ZIPs internos: **2** (ninguno es el árbol base completo).

| ZIP interior | SHA-256 | Archivos regulares en tar.gz | Aplicación |
|---|---|---:|---|
| `SM-A175F_16_Opensource_A175FXXS7CZI4_A175NKSS7CZI2.zip` | `643bde1afbf8dd17f98962314245818a0057ed46d6a22503f7ddeacd0478495b` | 21 | Actualización CZI4 internacional **SM-A175F** |
| `SM-A175N_KOR_16_Opensource_dts.zip` | `b85793860c5e760007bcb3f9e6eab009c7e98864222d78ab59149dcfe04b8631` | 22 | Configuración DTS/defconfig para **SM-A175N** coreano; **no superponer automáticamente** sobre variante F |

Ambos contienen un TXT con la instrucción:

> Download and unzip the kernel source of A175FXXU5CZE9.

Los tar.gz no incluyen `kernel-6.12/Makefile` ni `Module.symvers`. Ambos son **paquetes incrementales** y no permiten reproducir por sí solos una compilación de kernel.

## Comprobación realizada

Se analizaron ambos ZIP interiores y los dos TAR.GZ con Python 3, sin extraerlos ni ejecutarlos. Se probó el inspector:

```bash
python3 RTDriver-A17/scripts/inspect_a17_source_bundle.py SM-A175F.zip
```

Salida esperada relevante: `A175NKSU5CZE9_kernel.tar.gz -> 22 archivos`; `A175FXXS7CZI4_kernel.tar.gz -> 21 archivos`; los dos `required_base_versions` apuntan a `A175FXXU5CZE9`.

## Estado del port

- El paquete CZI4 de SM-A175F es el correcto como **overlay de actualización**.
- El paquete coreano no debe añadirse al build SM-A175F sin verificar DTS, configuraciones y compatibilidad específica.
- Sigue **faltando el paquete de fuentes base A175FXXU5CZE9** descargado desde Samsung Open Source.
- Sigue faltando el código fuente del RT Driver original; un módulo de prueba Kbuild no equivale al RT Driver funcional.
- Compilación ARM64 para CZI4, `Module.symvers` exacto, revisión de CFI y carga del driver en dispositivo: **pendientes**.

Buscar **fuentes Open Source**, no firmware de Odin:
https://opensource.samsung.com/

Firmware A175FXXU5CZE9 disponible en portales de firmware, pero esos paquetes binarios no sustituyen las fuentes de Samsung.
