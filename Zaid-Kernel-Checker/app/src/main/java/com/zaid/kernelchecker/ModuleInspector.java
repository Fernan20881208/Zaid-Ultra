package com.zaid.kernelchecker;

import java.nio.charset.StandardCharsets;

/** Reads .ko metadata, without loading or executing a module. Pure Java. */
public final class ModuleInspector {
    private ModuleInspector() {}

    public static final class Info {
        public final boolean elf64, arm64, relocatable;
        public final String vermagic, description;
        Info(boolean elf64, boolean arm64, boolean relocatable, String vermagic, String description) {
            this.elf64 = elf64;
            this.arm64 = arm64;
            this.relocatable = relocatable;
            this.vermagic = vermagic;
            this.description = description;
        }
        public String compatibility(String kernel) {
            if (!elf64 || !arm64 || !relocatable) return "Incompatible: requiere ELF64, AArch64 y ET_REL.";
            if (vermagic.isEmpty()) return "Indeterminado: archivo sin vermagic legible.";
            String k = vermagic.trim().split("\\s+", 2)[0];
            if (!k.equals(kernel.trim())) return "No coincide vermagic (" + k + ") con kernel (" + kernel + ").";
            return "Coincide versión de vermagic; NO garantiza KMI, símbolos, firma ni carga.";
        }
    }

    private static int u(byte[] b, int at) { return b[at] & 255; }

    public static Info inspect(byte[] b) {
        if (b == null || b.length < 64 || u(b, 0) != 127 || b[1] != 'E' || b[2] != 'L' || b[3] != 'F') {
            return new Info(false, false, false, "", "No es un ELF válido.");
        }
        boolean little = u(b, 5) == 1;
        boolean bits = u(b, 4) == 2;
        boolean reloc = little && u(b, 16) == 1 && u(b, 17) == 0;
        boolean arm = little && u(b, 18) == 183 && u(b, 19) == 0;
        if (!little) return new Info(false, false, false, "", "Solo se admite ELF little-endian.");
        byte[] key = "vermagic=".getBytes(StandardCharsets.US_ASCII);
        String vm = "";
        for (int i = 0; i <= b.length - key.length; i++) {
            boolean match = true;
            for (int j = 0; j < key.length; j++) {
                if (b[i + j] != key[j]) { match = false; break; }
            }
            if (match) {
                int start = i + key.length, end = start;
                while (end < b.length && end - start < 256 && b[end] != 0) end++;
                vm = new String(b, start, end - start, StandardCharsets.US_ASCII).trim();
                break;
            }
        }
        return new Info(bits, arm, reloc, vm, (bits ? "ELF64" : "ELF32") +
            " / " + (arm ? "AArch64" : "otro CPU") + " / " + (reloc ? "ET_REL" : "no ET_REL"));
    }
}
