#!/usr/bin/env python3
"""Verify compiled diagnostics without loading, signing or modifying the ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

TARGET = "6.12.38-android16-5-abA175FXXS7CZI4-4k"


def elf_header(data):
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("Expected a complete little-endian ELF64 header")
    elf_type, machine, version = struct.unpack_from("<HHI", data, 16)
    if (elf_type, machine, version) != (1, 183, 1):
        raise ValueError(f"Expected ET_REL AArch64: type={elf_type}, machine={machine}")
    return "ELF64 little-endian ET_REL AArch64"


def read_config(path):
    config = {}
    for line in path.read_text().splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            config[key] = value.strip('"')
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            config[line[2:-11]] = "n"
    return config


def expected_vermagic(config):
    parts = [TARGET]
    for key, value in (("CONFIG_SMP", "SMP"),):
        if config.get(key) == "y":
            parts.append(value)
    if config.get("CONFIG_PREEMPT_BUILD") == "y":
        parts.append("preempt")
    elif config.get("CONFIG_PREEMPT_RT") == "y":
        parts.append("preempt_rt")
    for key, value in (("CONFIG_MODULE_UNLOAD", "mod_unload"),
                       ("CONFIG_MODVERSIONS", "modversions")):
        if config.get(key) == "y":
            parts.append(value)
    parts.append("aarch64")
    return " ".join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report-dir", type=Path, required=True)
    parser.add_argument("--kernel-artifact-dir", type=Path, required=True)
    parser.add_argument("--config-dir", type=Path, required=True)
    parser.add_argument("--module-name", choices=("rt_a17_diag", "rt_a17"), default="rt_a17_diag")
    args = parser.parse_args()
    report = args.report_dir.resolve()
    report.mkdir(parents=True, exist_ok=True)
    ko = report / (args.module_name + ".ko")
    runtime = args.module_name == "rt_a17"
    prefix = "rt-port" if runtime else "rt-diag"
    result = {"expected_release": TARGET, "checks": {}, "errors": [],
              "device_abi_verified": False, "device_signature_acceptance_verified": False,
              "original_rt_functions_implemented": False}
    if runtime:
        del result["original_rt_functions_implemented"]
        result["original_rt_fully_implemented"] = False
        result["implemented_original_opcodes"] = ["0x801", "0x802", "0x803"]
        result["device_operations_tested"] = False

    def check(name, fn):
        try:
            result["checks"][name] = fn()
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            result["errors"].append(f"{name}: {error}")

    def command(name, argv):
        proc = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, check=False)
        (report / name).write_text(proc.stdout)
        if proc.returncode:
            raise ValueError(f"{argv[0]} exited {proc.returncode}; see {name}")
        return proc.stdout.strip()

    def binary():
        data = ko.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        result["sha256"] = digest
        (report / f"{prefix}-sha256.txt").write_text(f"{digest}  {ko.name}\n")
        result["signature_trailer_present"] = data.endswith(b"~Module signature appended~\n")
        return elf_header(data)

    check("elf", binary)
    check("file", lambda: command(f"{prefix}-file.txt", ["file", str(ko)]))
    check("readelf", lambda: command(f"{prefix}-elf-header.txt", ["readelf", "-h", str(ko)]))
    check("modinfo", lambda: command(f"{prefix}-modinfo.txt", ["modinfo", str(ko)]))
    for field in ("name", "version", "vermagic", "depends", "signer", "sig_key", "sig_hashalgo"):
        check(field, lambda field=field: command(f"{prefix}-{field}.txt",
                                                ["modinfo", "-F", field, str(ko)]))
    check("symbols", lambda: command(f"{prefix}-symbols.txt", ["readelf", "-Ws", str(ko)]))

    def config_and_release():
        config_path = args.config_dir / "out_dir/.config"
        if not config_path.is_file():
            raise ValueError(f"Missing canonical kernel config: {config_path}")
        shutil.copyfile(config_path, report / "kernel.config")
        config = read_config(config_path)
        result["kernel_config_sha256"] = hashlib.sha256(config_path.read_bytes()).hexdigest()
        result["kernel_options"] = {k: v for k, v in config.items() if
            k.startswith(("CONFIG_MODULE", "CONFIG_MODVERSIONS", "CONFIG_LOCALVERSION",
                          "CONFIG_CFI", "CONFIG_LTO", "CONFIG_TRIM_UNUSED_KSYMS"))}
        for key, expected in (("CONFIG_ARM64", "y"), ("CONFIG_MODULES", "y"),
                              ("CONFIG_MODVERSIONS", "y"), ("CONFIG_CFI_CLANG", "y"),
                              ("CONFIG_MODULE_SIG", "y"), ("CONFIG_LOCALVERSION", "-4k")):
            if config.get(key) != expected:
                raise ValueError(f"{key}={config.get(key)!r}, expected {expected!r}")
        release_path = args.kernel_artifact_dir / "include/config/kernel.release"
        shutil.copyfile(release_path, report / "kernel.release")
        release = release_path.read_text().strip()
        result["compiled_kernel_release"] = release
        if release != TARGET:
            raise ValueError(f"Compiled kernel release {release!r} != {TARGET!r}")
        magic = result["checks"].get("vermagic")
        expected = expected_vermagic(config)
        result["expected_vermagic"] = expected
        if magic != expected:
            raise ValueError(f"Full vermagic {magic!r} != config-derived {expected!r}")
        return "Kernel release and full config-derived vermagic match"

    check("config_and_release", config_and_release)

    def dependencies():
        if result["checks"].get("name") != args.module_name:
            raise ValueError("Unexpected module name")
        if result["checks"].get("depends") != "":
            raise ValueError("Unexpected external module dependencies")
        return "No external module dependencies; imported kernel symbols checked by CRC"

    check("dependencies", dependencies)
    if runtime:
        def runtime_profile():
            version = result["checks"].get("version")
            profiles = {
                "0.1": (["0x801", "0x802", "0x803"], 2, 0x7, 0xe),
                "0.2": (["0x801", "0x802", "0x803", "0x805", "0x806"], 3, 0x3f, 0x6e),
            }
            if version not in profiles:
                raise ValueError(f"Unknown rt_a17 module version {version!r}; binary retained")
            ops, abi, caps, mask = profiles[version]
            result["implemented_original_opcodes"] = ops
            result["port_abi"] = abi
            result["capabilities"] = caps
            result["implemented_opcode_mask"] = mask
            result["original_rt_fully_implemented"] = False
            result["device_operations_tested"] = False
            return f"Version {version}, ABI {abi}, code capability mask {caps:#x}; device unverified"
        check("runtime_profile", runtime_profile)
    check("crc", lambda: command(f"{prefix}-crc.txt", ["bash", str(Path(__file__).with_name(
        "verify_module_crc.sh")), str(ko), str(report / "Module.symvers")]))
    symvers = report / "Module.symvers"
    if symvers.is_file():
        result["module_symvers_sha256"] = hashlib.sha256(symvers.read_bytes()).hexdigest()
    signer = result["checks"].get("signer", "")
    key = result["checks"].get("sig_key", "")
    result["signature_state"] = ("signed; device trust unverified" if signer and key else
        "signature trailer present but signer unresolved" if result.get("signature_trailer_present")
        else "unsigned")
    if bool(signer or key) != bool(result.get("signature_trailer_present")):
        result["errors"].append("Inconsistent module signature metadata")
    result["build_checks_passed"] = not result["errors"]
    (report / f"{prefix}-verification.json").write_text(json.dumps(result, indent=2) + "\n")
    description = (["Partial port: original 0x801/0x802/0x803 implemented for bounded data-memory",
                    "read/write and file-mapping base lookup. Original key, gyro and breakpoint",
                    "operations remain unsupported. Device execution and original app unverified."]
                   if runtime else
                   ["This is a newly compiled diagnostic module. Original RT memory and breakpoint",
                    "operations are not implemented."])
    lines = [f"# Samsung A17 {args.module_name} build verification", "",
             f"Build checks passed: **{result['build_checks_passed']}**",
             f"SHA-256: `{result.get('sha256', 'unavailable')}`",
             f"Vermagic: `{result['checks'].get('vermagic', 'unavailable')}`",
             f"Signature: **{result['signature_state']}**", "",
             *description, "No module was loaded or installed.",
             "CRC comparison covers this build's Module.symvers; the running phone's KMI/ABI",
             "and signature acceptance are unverified.", "", "## Errors", ""]
    lines.extend([f"- {e}" for e in result["errors"]] or ["None in build checks."])
    (report / f"{prefix}-report.md").write_text("\n".join(lines) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "checks"}, indent=2))
    return 0 if result["build_checks_passed"] else 46


if __name__ == "__main__":
    raise SystemExit(main())
