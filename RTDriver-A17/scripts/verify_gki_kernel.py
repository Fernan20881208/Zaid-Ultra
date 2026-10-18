#!/usr/bin/env python3
"""Check a custom ARM64 Image and real built-in RT evidence, never load it."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tarfile
import tempfile


def config(path):
    result = {}
    for line in path.read_text().splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
        elif re.fullmatch(r"# CONFIG_\w+ is not set", line):
            result[line.split()[1]] = "n"
    return result


def compare_config(reference, actual):
    before, after = config(reference), config(actual)
    diff = {k: {"stock": before.get(k, "n"), "custom": after.get(k, "n")}
            for k in before.keys() | after.keys() if before.get(k, "n") != after.get(k, "n")}
    expected = {"CONFIG_RT_A17": {"stock": "n", "custom": "y"},
                "CONFIG_LOCALVERSION": {"stock": '"-4k"', "custom": '"-4k-rt-taipei"'}}
    if diff != expected:
        raise ValueError("Unexpected kernel configuration changes: " + json.dumps(diff, sort_keys=True))
    return diff


def symbol_versions(path):
    result = {}
    for line in path.read_text().splitlines():
        fields = line.split()
        if len(fields) >= 4:
            if fields[1] in result:
                raise ValueError("Duplicate exported symbol " + fields[1])
            result[fields[1]] = (fields[0].lower(), fields[3], fields[4] if len(fields) > 4 else "")
    if not result:
        raise ValueError("Empty Module.symvers")
    return result


def verify(workspace, report):
    compare = compare_config(workspace / "reference/kernel_aarch64_dot_config", report / "kernel.config")
    expected = symbol_versions(workspace / "reference/kernel_aarch64_Module.symvers")
    actual = symbol_versions(report / "Module.symvers")
    differences = {k: {"stock": expected.get(k), "custom": actual.get(k)}
                   for k in expected.keys() | actual.keys() if expected.get(k) != actual.get(k)}
    (report / "kmi-crc-diff.json").write_text(json.dumps(differences, indent=2) + "\n")
    if differences:
        raise ValueError(f"{len(differences)} exported-symbol/CRC differences; keep binaries as diagnostic evidence")
    with (report / "vmlinux").open("rb") as handle:
        elf = handle.read(64)
    if len(elf) < 64 or elf[:6] != b"\x7fELF\x02\x01":
        raise ValueError("vmlinux must be an ELF64 little-endian linked kernel")
    elf_type, machine = struct.unpack_from("<HH", elf, 16)
    # The stock ARM64 kernel is ET_DYN (3): arch/arm64/Makefile links with
    # -shared -Bsymbolic for CONFIG_RELOCATABLE=y. ET_REL is only an object
    # file and must never count as a linked kernel image.
    expected_type = 3 if config(report / "kernel.config").get("CONFIG_RELOCATABLE") == "y" else 2
    if machine != 183 or elf_type != expected_type:
        raise ValueError("vmlinux ELF type/architecture disagrees with its ARM64 configuration")
    image = (report / "Image").read_bytes()
    if len(image) < 4096 or image[56:60] != b"ARM\x64":
        raise ValueError("Not an ARM64 Linux Image")
    match = re.search(rb"Linux version ([^ \x00]+) ", image)
    if not match:
        raise ValueError("Linux version banner absent from Image")
    release = match.group(1).decode("ascii")
    if not release.startswith("6.12.58-android16-6-") or "-rt-taipei" not in release:
        raise ValueError("Custom release missing expected baseline or its RT identity: " + release)
    (report / "kernel.release").write_text(release + "\n")
    symbols = {line.split()[-1] for line in (report / "System.map").read_text().splitlines() if line.split()}
    required = {"rt_a17_init", "rt_a17_ioctl", "rt_a17_fops", "rt_bp_ioctl", "rt_bp_hit",
                "register_user_hw_breakpoint", "modify_user_hw_breakpoint", "unregister_hw_breakpoint"}
    if not required <= symbols or not any("initcall" in s and "rt_a17_init" in s for s in symbols):
        raise ValueError("Missing linked RT/breakpoint symbols or its built-in initcall")
    builtin = "kernel/drivers/misc/rt_a17/runtime/rt_a17.ko"
    if builtin not in (report / "modules.builtin").read_text().splitlines():
        raise ValueError("RT absent from modules.builtin")
    if b"rt_a17.version=0.2" not in (report / "modules.builtin.modinfo").read_bytes():
        raise ValueError("Full RT backend metadata absent")
    module_signers = set()
    module_count = 0
    with tempfile.TemporaryDirectory() as tmp, tarfile.open(report / "modules_staging_dir.tar.gz") as archive:
        for member in archive:
            if not member.isfile() or not member.name.endswith(".ko"):
                continue
            data = archive.extractfile(member).read()
            if not data.endswith(b"~Module signature appended~\n"):
                raise ValueError("Unsigned rebuilt GKI module: " + member.name)
            path = Path(tmp) / "module.ko"
            path.write_bytes(data)
            signer = subprocess.check_output(["modinfo", "-F", "signer", str(path)], text=True).strip()
            vermagic = subprocess.check_output(["modinfo", "-F", "vermagic", str(path)], text=True).strip()
            if not signer or not vermagic.startswith(release + " "):
                raise ValueError("GKI module metadata/signature mismatch: " + member.name)
            module_signers.add(signer)
            module_count += 1
    if not module_count:
        raise ValueError("No matching rebuilt GKI modules supplied")
    evidence = json.loads((report / "kernel-input-verification.json").read_text())
    evidence.update({"kernel_rebuilt": True, "kernel_release": release,
                     "vmlinux_elf_type": "ET_DYN" if elf_type == 3 else "ET_EXEC",
                     "configuration_diff": compare, "matching_exported_crcs": len(actual),
                     "linked_rt_symbols": sorted(required), "rt_initcall_present": True,
                     "gki_modules_signed": module_count, "gki_module_signers": sorted(module_signers),
                     "kernel_image_avb_signed": False, "stock_google_signing_key_reused": False,
                     "device_boot_verified": False, "functional_verified": False})
    (report / "kernel-verification.json").write_text(json.dumps(evidence, indent=2) + "\n")
    (report / "kernel-report.md").write_text(f"""# Kernel Moto g55 taipei con RT integrado

Image ARM64 y vmlinux AArch64 compilados desde la fuente oficial fijada del build 14987696.
Release real: `{release}`. CONFIG_RT_A17=y: memoria, base de mapeos y breakpoints de un disparo.
ABI 3, capacidades 0x3f y máscara 0x6e. INIT_KEY/GYRO_CONFIG: EOPNOTSUPP.

{len(actual)} símbolos/CRC coinciden con el Module.symvers original. La configuración
solo cambia CONFIG_RT_A17 y CONFIG_LOCALVERSION. Se conservan firma, CFI, KMI y permisos.
modules.builtin, initcall y símbolos enlazados prueban inclusión, no ejecución física.

Se incluyen {module_count} módulos GKI recompilados y firmados con la clave nueva generada
por Kbuild, y su certificado público. No se entrega la clave privada. Los módulos GKI
protegidos originales de Google no se dan por aceptados por este kernel personalizado.
La imagen raw no tiene firma AVB ni se presenta como boot.img listo para flashear.

No se proporcionó boot.img stock del Moto para verificar su empaquetado. Arranque,
drivers vendor del teléfono, hardware de breakpoints y APK original: pendientes.
functional_verified=false. Ningún paso carga módulos, flashea o modifica el teléfono.
""")
    hashes = []
    for path in sorted(report.iterdir()):
        if path.is_file() and path.name != "SHA256SUMS":
            with path.open("rb") as handle:
                digest = hashlib.file_digest(handle, "sha256").hexdigest()
            hashes.append(f"{digest}  {path.name}")
    (report / "SHA256SUMS").write_text("\n".join(hashes) + "\n")
    print(f"Verified real Image, built-in RT, {len(actual)} CRCs and {module_count} signed GKI modules.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--config-only", type=Path)
    args = parser.parse_args()
    if args.config_only:
        print(json.dumps(compare_config(args.workspace / "reference/kernel_aarch64_dot_config",
                                        args.config_only), indent=2))
    else:
        verify(args.workspace, args.report)
