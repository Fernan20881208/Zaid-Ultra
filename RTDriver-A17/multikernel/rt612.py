#!/usr/bin/env python3
"""Build/verify/package RT modules per ARM64 6.12 kernel; never load or patch them."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zipfile

HERE = Path(__file__).resolve().parent
RELEASE = re.compile(r"6\.12\.[0-9]+(?:[-+._a-zA-Z0-9]*)\Z")
REQUIRED_EXPORTS = {"mm_access", "pin_user_pages_remote", "get_task_mm", "get_task_cred",
                    "register_user_hw_breakpoint", "modify_user_hw_breakpoint", "unregister_hw_breakpoint"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def config_values(text):
    result = {}
    for line in text.splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            result[key] = value.strip('"')
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            result[line[2:-11]] = "n"
    return result


def check_config(config, release):
    if not RELEASE.fullmatch(release):
        raise ValueError("Expected a complete 6.12.x kernel.release; no prefix matching")
    for key in ("CONFIG_ARM64", "CONFIG_64BIT", "CONFIG_MMU", "CONFIG_MODULES",
                "CONFIG_PERF_EVENTS", "CONFIG_HAVE_HW_BREAKPOINT"):
        if config.get(key) != "y":
            raise ValueError(f"Required {key}=y, got {config.get(key)!r}")
    if config.get("CONFIG_RANDSTRUCT_FULL") == "y" or config.get("CONFIG_RANDSTRUCT_PERFORMANCE") == "y":
        raise ValueError("RANDSTRUCT builds need a dedicated profile and matching generated seed")
    return config


def vermagic(config, release):
    values = [release]
    if config.get("CONFIG_SMP") == "y": values.append("SMP")
    if config.get("CONFIG_PREEMPT_BUILD") == "y": values.append("preempt")
    elif config.get("CONFIG_PREEMPT_RT") == "y": values.append("preempt_rt")
    for key, token in (("CONFIG_MODULE_UNLOAD", "mod_unload"), ("CONFIG_MODVERSIONS", "modversions")):
        if config.get(key) == "y": values.append(token)
    return " ".join(values + ["aarch64"])


def sections(data):
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HHI", data, 16) != (1, 183, 1):
        raise ValueError("Expected ELF64 little-endian ET_REL AArch64")
    offset = struct.unpack_from("<Q", data, 40)[0]
    entry_size, count, string_index = struct.unpack_from("<HHH", data, 58)
    if entry_size != 64 or not offset or offset + 64 > len(data):
        raise ValueError("Invalid ELF section table")
    first = struct.unpack_from("<IIQQQQIIQQ", data, offset)
    count = count or first[5]
    if string_index == 0xffff: string_index = first[6]
    if not 0 < count <= 65536 or string_index >= count or offset + count * 64 > len(data):
        raise ValueError("Truncated ELF section table")
    headers = [struct.unpack_from("<IIQQQQIIQQ", data, offset + i * 64) for i in range(count)]
    def content(header):
        pos, size = header[4:6]
        if header[1] == 8: return b""  # NOBITS does not occupy file bytes.
        if pos + size > len(data): raise ValueError("Truncated ELF section")
        return data[pos:pos + size]
    names = content(headers[string_index])
    result = {}
    for header in headers[1:]:
        start = header[0]
        end = names.find(b"\0", start)
        if start >= len(names) or end < 0: raise ValueError("Invalid ELF section name")
        name = names[start:end].decode("ascii")
        if name in result: raise ValueError(f"Duplicate ELF section {name}")
        result[name] = (header, content(header))
    return result, headers, content


def inspect_module(data, symvers, config, release, name):
    sec, headers, content = sections(data)
    fields = {}
    for entry in sec.get(".modinfo", (None, b""))[1].split(b"\0"):
        if b"=" in entry:
            key, value = entry.decode("utf-8").split("=", 1)
            if key in fields and key in ("name", "version", "vermagic", "depends", "license"):
                raise ValueError(f"Duplicate module field {key}")
            fields[key] = value.strip()
    expected = vermagic(config, release)
    if fields.get("name") != name or fields.get("vermagic") != expected:
        raise ValueError(f"Module name/vermagic mismatch: {fields.get('name')!r}, {fields.get('vermagic')!r}")
    if fields.get("depends", "") or fields.get("license") != "GPL":
        raise ValueError("Unexpected external dependencies or module license")
    if name == "rt_a17" and fields.get("version") != "0.2":
        raise ValueError("Unrecognized RT port version")
    exported = {}
    for line in symvers.decode().splitlines():
        row = line.split()
        if len(row) < 4: raise ValueError("Malformed kernel Module.symvers")
        crc = int(row[0], 16)
        if row[1] in exported and exported[row[1]] != crc:
            raise ValueError(f"Conflicting kernel CRCs for {row[1]}")
        exported[row[1]] = crc
    imported = set()
    if ".symtab" not in sec: raise ValueError("Missing module symbol table")
    header, symbols = sec[".symtab"]
    if header[9] != 24 or len(symbols) % 24 or header[6] >= len(headers):
        raise ValueError("Invalid ELF symbol table")
    strings = content(headers[header[6]])
    for i in range(0, len(symbols), 24):
        pos, info, _, index, _, _ = struct.unpack_from("<IBBHQQ", symbols, i)
        if index == 0 and pos and info >> 4 in (1, 2):
            end = strings.find(b"\0", pos)
            if pos >= len(strings) or end < 0: raise ValueError("Invalid imported symbol name")
            imported.add(strings[pos:end].decode("ascii"))
    missing = sorted(imported - exported.keys())
    if missing: raise ValueError(f"Imports absent from target Module.symvers: {missing}")
    versions = {}
    if "__versions" in sec:
        table = sec["__versions"][1]
        if len(table) % 64: raise ValueError("Invalid basic modversions table")
        for i in range(0, len(table), 64):
            symbol = table[i + 8:i + 64].split(b"\0", 1)[0].decode("ascii")
            if not symbol or symbol in versions: raise ValueError("Invalid/duplicate CRC symbol")
            versions[symbol] = struct.unpack_from("<Q", table, i)[0]
    if "__version_ext_names" in sec or "__version_ext_crcs" in sec:
        names = [s.decode("ascii") for s in sec["__version_ext_names"][1].split(b"\0") if s]
        raw = sec["__version_ext_crcs"][1]
        if len(raw) != len(names) * 4 or len(names) != len(set(names)):
            raise ValueError("Invalid extended modversions tables")
        extended = dict(zip(names, struct.unpack("<" + "I" * len(names), raw)))
        if versions and extended != versions: raise ValueError("Basic/extended CRC tables disagree")
        versions = extended
    if config.get("CONFIG_MODVERSIONS") == "y":
        if not versions or imported - versions.keys(): raise ValueError("Missing imported-symbol CRCs")
        for symbol, crc in versions.items():
            if exported.get(symbol) != crc: raise ValueError(f"Target CRC mismatch for {symbol}")
    elif versions:
        raise ValueError("Module has CRCs but target config disables MODVERSIONS")
    signed = data.endswith(b"~Module signature appended~\n")
    if config.get("CONFIG_MODULE_SIG_FORCE") == "y" and not signed:
        raise ValueError("Unsigned module with CONFIG_MODULE_SIG_FORCE=y; binary retained as diagnostic")
    return {"name": name, "version": fields.get("version"), "elf": "ET_REL AArch64",
            "sha256": digest(data), "size": len(data), "vermagic": expected,
            "crc_matches": len(versions), "imports": len(imported), "external_dependencies": [],
            "signature": "trailer_present_trust_unverified" if signed else "unsigned",
            "device_tested": False, "device_compatible_verified": False, "original_rt_fully_verified": False}


def preflight(source, output):
    makefile = (source / "Makefile").read_text()
    for key, expected in (("VERSION", "6"), ("PATCHLEVEL", "12")):
        if not re.search(rf"^{key}\s*=\s*{expected}\s*$", makefile, re.M):
            raise ValueError("Kernel source is not Linux 6.12")
    config = config_values((output / ".config").read_text())
    release = (output / "include/config/kernel.release").read_text().strip()
    check_config(config, release)
    sublevel = re.search(r"^SUBLEVEL\s*=\s*([0-9]+)\s*$", makefile, re.M)
    if not sublevel or not re.match(rf"6\.12\.{int(sublevel[1])}(?:[-+._a-zA-Z]|$)", release):
        raise ValueError("Source SUBLEVEL disagrees with generated kernel.release")
    header = (output / "include/generated/utsrelease.h").read_text()
    if f'"{release}"' not in header: raise ValueError("Generated utsrelease.h disagrees with kernel.release")
    auto = (output / "include/generated/autoconf.h").read_text()
    for key in ("CONFIG_ARM64", "CONFIG_MODULES", "CONFIG_MODVERSIONS", "CONFIG_PERF_EVENTS", "CONFIG_HAVE_HW_BREAKPOINT"):
        defined = bool(re.search(rf"^#define {key} 1$", auto, re.M))
        if defined != (config.get(key) == "y"): raise ValueError(f"Stale autoconf.h for {key}")
    symvers = (output / "Module.symvers").read_bytes()
    if not symvers.strip(): raise ValueError("A real kernel build Module.symvers is required")
    names = {row.split()[1] for row in symvers.decode().splitlines() if len(row.split()) >= 4}
    if REQUIRED_EXPORTS - names:
        raise ValueError(f"Kernel does not export required APIs: {sorted(REQUIRED_EXPORTS - names)}")
    return config, release, symvers


def build(args):
    source, out = args.kernel_source.resolve(), args.kernel_out.resolve()
    config, release, symvers = preflight(source, out)
    info = {"kernel_release": release, "expected_vermagic": vermagic(config, release),
            "module_symvers_sha256": digest(symvers), "signature_required": config.get("CONFIG_MODULE_SIG_FORCE") == "y",
            "source_api_compilation_tested": False, "device_tested": False}
    if args.check_only:
        print(json.dumps(info, indent=2)); return
    result = args.output.resolve()
    if result.is_relative_to(HERE.parent / "rt_port") or result.is_relative_to(HERE):
        raise ValueError("Output must be outside the source directories being copied")
    result.mkdir(parents=True, exist_ok=False)
    staged = result / "source"
    shutil.copytree(HERE.parent / "rt_port", staged,
                    ignore=shutil.ignore_patterns("tests", "__pycache__"))
    command = ["make", "-C", str(source), f"O={out}", "ARCH=arm64", f"LLVM={args.llvm}", "LLVM_IAS=1"]
    try:
        with (result / "build.log").open("w") as log:
            for directory in (staged, staged / "runtime"):
                subprocess.run(command + [f"M={directory}", f"-j{min(os.cpu_count() or 2, 16)}", "modules"],
                               stdout=log, stderr=subprocess.STDOUT, check=True)
        modules = {}
        for name, directory in (("rt_a17_diag", staged), ("rt_a17", staged / "runtime")):
            binary = (directory / f"{name}.ko").read_bytes()
            modules[name] = inspect_module(binary, symvers, config, release, name)
            (result / f"{name}.ko").write_bytes(binary)
        (result / "Module.symvers").write_bytes(symvers)
        shutil.copyfile(out / ".config", result / "kernel.config")
        (result / "kernel.release").write_text(release + "\n")
        info.update(build_checks_passed=True, source_api_compilation_tested=True, modules=modules)
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        info.update(build_checks_passed=False, error=str(exc))
        raise
    finally:
        (result / "verification.json").write_text(json.dumps(info, indent=2) + "\n")
    print(json.dumps(info, indent=2))


def bundle(args):
    index = json.loads((HERE / "profiles.json").read_text())
    profile = next(p for p in index["profiles"] if p["id"] == args.profile)
    archive = args.artifact_zip.read_bytes()
    if digest(archive) != profile["build"]["archive_sha256"]:
        raise ValueError("Actions artifact ZIP digest differs from the pinned profile")
    for name, expected in profile["source_sha256"].items():
        if digest((HERE.parent / name).read_bytes()) != expected:
            raise ValueError(f"Packaged source differs from the compiled profile: {name}")
    with zipfile.ZipFile(args.artifact_zip) as z:
        names = z.namelist()
        if len(names) != len(set(names)): raise ValueError("Duplicate archive paths")
        files = {n: z.read(n) for n in profile["sha256"]}
        for n, expected in profile["sha256"].items():
            if digest(files[n]) != expected: raise ValueError(f"Pinned file digest mismatch: {n}")
        release = z.read("kernel.release").decode().strip()
        if release != profile["kernel_release"]: raise ValueError("Pinned release differs from artifact")
        config = check_config(config_values(files["kernel.config"].decode()), release)
        if vermagic(config, release) != profile["vermagic"]: raise ValueError("Profile config/vermagic disagreement")
        checks = {name: inspect_module(files[name + ".ko"], files["Module.symvers"], config, release, name)
                  for name in ("rt_a17", "rt_a17_diag")}
        if any(m["signature"] != profile["signature"] for m in checks.values()):
            raise ValueError("Unexpected module signature state")
        evidence = {n: z.read(n) for n in ("source-commit.txt", "clang-version.txt", "rt-port-modinfo.txt",
                    "rt-diag-modinfo.txt", "rt-port-crc.txt", "rt-diag-crc.txt", "rt-port-userspace-compiler.txt")}
        if evidence["source-commit.txt"].decode().strip() != profile["build"]["source_commit"]:
            raise ValueError("Artifact source commit mismatch")
        tools = {n: z.read(n) for n in ("rt_a17_port_status", "rt_a17_selftest", "rt_a17_bp_selftest")}
        declared = {}
        for line in z.read("rt-port-tools-sha256.txt").decode().splitlines():
            sha, path = line.split(); declared[Path(path).name] = sha
        for n, data in tools.items():
            if digest(data) != declared.get(n): raise ValueError(f"Tool hash mismatch: {n}")
    destination = args.output.resolve()
    if destination.is_relative_to(HERE.parent):
        raise ValueError("Bundle output must be outside RTDriver-A17 source directories")
    destination.mkdir(parents=True, exist_ok=False)
    target = destination / "targets" / profile["id"]
    target.mkdir(parents=True)
    for name, data in files.items(): (target / name).write_bytes(data)
    (target / "kernel.release").write_text(release + "\n")
    (target / "architecture").write_text("aarch64\n")
    (target / "device.model").write_text(profile["model"] + "\n")
    (target / "verification.json").write_text(json.dumps({"modules": checks, "profile": profile}, indent=2) + "\n")
    for name, data in evidence.items():
        (target / name).write_bytes(data)
    (target / "tools").mkdir()
    for name, data in tools.items():
        p = target / "tools" / name; p.write_bytes(data); p.chmod(0o755)
    checksum = "".join(f"{digest(p.read_bytes())}  {p.relative_to(target)}\n" for p in sorted(target.rglob("*")) if p.is_file())
    (target / "SHA256SUMS").write_text(checksum)
    source_root = destination / "RTDriver-A17"
    for directory in ("rt_port", "multikernel"):
        shutil.copytree(HERE.parent / directory, source_root / directory,
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    shutil.copyfile(HERE / "README.md", destination / "README.md")
    (destination / "profiles.json").write_text(json.dumps(index, indent=2) + "\n")
    result = {"kernel_family": "6.12", "single_universal_binary": False, "compiled_profiles": [profile["id"]],
              "modules": checks, "device_tested": False, "source_archive_sha256": digest(archive)}
    result["kit_source_commit"] = os.environ.get("GITHUB_SHA")
    (destination / "bundle-verification.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    cmd = commands.add_parser("build")
    cmd.add_argument("--kernel-source", type=Path, required=True)
    cmd.add_argument("--kernel-out", type=Path, required=True)
    cmd.add_argument("--llvm", default="1", help="Kbuild LLVM= value; use the target's original Clang directory")
    cmd.add_argument("--output", type=Path)
    cmd.add_argument("--check-only", action="store_true")
    cmd = commands.add_parser("bundle")
    cmd.add_argument("--profile", default="samsung-a17-czi4")
    cmd.add_argument("--artifact-zip", type=Path, required=True)
    cmd.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "build":
            if not args.check_only and args.output is None: parser.error("build requires --output unless --check-only")
            build(args)
        else: bundle(args)
    except (OSError, ValueError, KeyError, StopIteration, struct.error, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr); return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
