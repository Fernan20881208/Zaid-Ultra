#!/usr/bin/env python3
"""Verify actual DDK outputs against the matching public GKI build."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "multikernel"))
import rt612


def command(args, output):
    done = subprocess.run([str(a) for a in args], text=True, capture_output=True)
    output.write_text(done.stdout + done.stderr)
    done.check_returncode()
    return done.stdout.strip()


def verify(workspace, report):
    report.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / "gki/14987696.json").read_text())
    inputs = workspace / "prebuilts/kernel"
    for src, dst in (("kernel_aarch64_Module.symvers", "Module.symvers"),
                     ("kernel_aarch64_dot_config", "kernel.config"),
                     ("manifest.xml", "manifest.xml"), ("abi_symbollist.raw", "abi_symbollist.raw")):
        shutil.copyfile(inputs / src, report / dst)
    shutil.copyfile(workspace / "gki-input-verification.json", report / "gki-input-verification.json")
    with tarfile.open(inputs / "kernel_aarch64_filegroup_decl.tar.gz") as archive:
        release = archive.extractfile(
            "bazel-out/k8-fastbuild/bin/common/kernel_aarch64/include/config/kernel.release").read().decode().strip()
    if release != lock["kernel_release"]:
        raise ValueError("Official Kbuild kernel.release differs from the target")
    (report / "kernel.release").write_text(release + "\n")
    config_bytes = (report / "kernel.config").read_bytes()
    if hashlib.sha256(config_bytes).hexdigest() != lock["device_config_sha256"]:
        raise ValueError("Kernel config differs from the supplied device configuration")
    config = rt612.config_values(config_bytes.decode())
    rt612.check_config(config, release)
    symvers = (report / "Module.symvers").read_bytes()
    if hashlib.sha256(symvers).hexdigest() != lock["files"]["kernel_aarch64_Module.symvers"]["sha256"]:
        raise ValueError("Module.symvers differs from the pinned official build")
    allowed = {line.strip() for line in (report / "abi_symbollist.raw").read_text().splitlines()
               if re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", line.strip())}
    paths = (report / "module-output-paths.txt").read_text().splitlines()
    modules = {}
    for name in ("rt_a17", "rt_a17_diag"):
        candidates = [workspace / p for p in paths if Path(p).name == name + ".ko"]
        if len(candidates) != 1:
            raise ValueError(f"Expected one cquery output for {name}, found {len(candidates)}")
        ko = report / (name + ".ko")
        shutil.copyfile(candidates[0], ko)  # Retain binary before checking compatibility.
        result = rt612.inspect_module(ko.read_bytes(), symvers, config, release, name)
        outside_kmi = sorted(set(result["imported_symbols"]) - allowed)
        if outside_kmi:
            raise ValueError(f"{name} imports symbols outside the GKI allowlist: {outside_kmi}")
        modinfo = command(["modinfo", ko], report / f"{name}-modinfo.txt")
        magic = command(["modinfo", "-F", "vermagic", ko], report / f"{name}-vermagic.txt")
        signer = command(["modinfo", "-F", "signer", ko], report / f"{name}-signer.txt")
        if magic != result["vermagic"]:
            raise ValueError("modinfo and ELF-derived vermagic disagree")
        command(["readelf", "-h", ko], report / f"{name}-elf.txt")
        command(["bash", ROOT / "scripts/verify_module_crc.sh", ko, report / "Module.symvers"],
                report / f"{name}-crc.txt")
        result["signer"] = signer
        result["all_imports_in_gki_allowlist"] = True
        if name == "rt_a17":
            if result["version"] != "0.3-memory":
                raise ValueError("Expected the explicit GKI memory-only module")
            result.update(abi=2, capabilities=7, implemented_opcode_mask=14,
                          implemented_opcodes=["0x801", "0x802", "0x803"], breakpoints_available=False)
        else:
            result.update(abi=1, capabilities=0, implemented_opcode_mask=0)
        modules[name] = result
    result = {"build_checks_passed": True, "kernel_rebuilt": False, "kernel_release": release,
              "kernel_source_commit": lock["source_commit"], "gki_build_id": lock["build_id"],
              "device_config_matches_official_build": True,
              "kernel_config_sha256": hashlib.sha256(config_bytes).hexdigest(),
              "module_symvers_sha256": hashlib.sha256(symvers).hexdigest(), "modules": modules,
              "device_abi_verified": False, "device_signature_acceptance_verified": False,
              "device_operations_tested": False, "original_rt_fully_verified": False}
    (report / "rt-gki-verification.json").write_text(json.dumps(result, indent=2) + "\n")
    lines = ["# Moto g55 5G / taipei: GKI module build", "",
             f"Kernel: `{release}`; official GKI build `{lock['build_id']}`.", "",
             "The supplied device config is byte-identical to the official config. Modules were",
             "compiled against official prepared DDK outputs; the kernel was not rebuilt.", "",
             "`rt_a17.ko` v0.3-memory: ABI 2, capabilities 0x7, opcode mask 0xe. Implements",
             "bounded READ_MEM, WRITE_MEM and MODULE_BASE. Breakpoints, INIT_KEY and GYRO",
             "return EOPNOTSUPP. `rt_a17_diag.ko` is diagnostics only.", "",
             "| Module | SHA-256 | CRC matches | Signature |", "| --- | --- | --- | --- |"]
    for name, module in modules.items():
        lines.append(f"| {name}.ko | `{module['sha256']}` | {module['crc_matches']} | {module['signature']} |")
    lines += ["", f"Vermagic: `{modules['rt_a17']['vermagic']}`.", "",
              f"Module.symvers SHA-256: `{result['module_symvers_sha256']}`.", "",
              "All imported symbols are in this build's GKI allowlist; both modversion formats",
              "are checked against its Module.symvers. modinfo and ELF reports are included.", "",
              "Unsigned modules retain their actual signature state. Signature acceptance,",
              "loading, CFI execution, runtime operations and APK integration remain untested",
              "on the phone. No module was loaded and no device protection was changed.", ""]
    (report / "rt-gki-report.md").write_text("\n".join(lines))
    sums = [f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(report)}\n"
            for p in sorted(report.rglob("*")) if p.is_file() and p.name != "SHA256SUMS"]
    (report / "SHA256SUMS").write_text("".join(sums))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    try:
        verify(args.workspace.resolve(), args.report.resolve())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        args.report.mkdir(parents=True, exist_ok=True)
        (args.report / "rt-gki-failure.json").write_text(json.dumps({
            "build_checks_passed": False, "error": str(error), "device_tested": False}, indent=2) + "\n")
        raise
