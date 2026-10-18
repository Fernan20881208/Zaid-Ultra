#!/usr/bin/env python3
"""Stage the exact official GKI source/tooling and the full built-in RT backend."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

from prepare_gki_ddk import ROOT, LOCK, clone_project, download_one, sha


REFERENCE_FILES = ("manifest_14987696.xml", "kernel_aarch64_dot_config",
                   "kernel_aarch64_Module.symvers")
EXTRA_PROJECTS = {"common", "common-modules/trusty", "common-modules/virtio-media",
                  "common-modules/virtual-device", "tools/mkbootimg", "external/avb"}
SOURCE_FILES = ("rt_abi.h", "runtime/rt_main.c", "runtime/rt_breakpoints.c",
                "runtime/rt_breakpoints.h", "runtime/rt_port_abi.h")


def stage(common):
    """Use normal Kconfig/Kbuild; do not edit exports, signatures or the ELF."""
    dest = common / "drivers/misc/rt_a17"
    dest.mkdir(parents=True, exist_ok=True)
    for name in SOURCE_FILES:
        target = dest / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / "rt_port" / name, target)
    shutil.copyfile(ROOT / "gki/kernel/Kconfig", dest / "Kconfig")
    shutil.copyfile(ROOT / "gki/kernel/Kbuild", dest / "runtime/Makefile")
    (dest / "Makefile").write_text("# SPDX-License-Identifier: GPL-2.0-only\nobj-y += runtime/\n")
    for name, hook in (("Kconfig", 'source "drivers/misc/rt_a17/Kconfig"'),
                       ("Makefile", "obj-$(CONFIG_RT_A17) += rt_a17/")):
        path = common / "drivers/misc" / name
        contents = path.read_text()
        if hook not in contents.splitlines():
            path.write_text(contents.rstrip() + "\n\n" + hook + "\n")
    shutil.copyfile(ROOT / "gki/kernel/rt_taipei_defconfig", common / "rt_taipei_defconfig")
    # Include newly added source in the real Git diff and in official dirty
    # SCM stamping. This is an intentional source modification, not the stock
    # Google binary, and must not impersonate its release/build identity.
    subprocess.run(["git", "-C", str(common), "add", "-N", "drivers/misc/rt_a17",
                    "rt_taipei_defconfig"], check=True)
    return {str(p.relative_to(common)): sha(p.read_bytes())
            for p in sorted(dest.rglob("*")) if p.is_file()}


def prepare(workspace, cache):
    lock = json.loads(LOCK.read_text())
    reference = workspace / "reference"
    reference.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=3) as pool:
        hashes = dict(pool.map(lambda n: download_one(n, lock["files"][n], lock,
                                                      cache, reference), REFERENCE_FILES))
    manifest = ET.fromstring((reference / "manifest.xml").read_text())
    projects = [p for p in manifest.findall("project")
                if p.get("path") in EXTRA_PROJECTS or
                set(p.get("groups", "").replace(",", " ").split()) & {"ddk", "ddk-external"}]
    common_project = next(p for p in projects if p.get("path") == "common")
    if common_project.get("revision") != lock["source_commit"]:
        raise ValueError("Official source manifest does not match the pinned baseline")
    with ThreadPoolExecutor(max_workers=4) as pool:
        for future in [pool.submit(clone_project, p, workspace) for p in projects]:
            future.result()
    compiler = workspace / "prebuilts/clang/host/linux-x86/clang-r536225/bin/clang"
    version = subprocess.check_output([str(compiler), "--version"], text=True)
    if "Android (14043575, +pgo, +bolt, +lto, +mlgo, based on r536225)" not in version:
        raise ValueError("Compiler differs from the device baseline")
    staged = stage(workspace / "common")
    patch = subprocess.check_output(["git", "-C", str(workspace / "common"), "diff", "--binary"])
    (workspace / "rt-integration.patch").write_bytes(patch)
    evidence = {"baseline_build": lock["build_id"], "baseline_source": lock["source_commit"],
                "official_input_sha256": hashes, "staged_source_sha256": staged,
                "integration_patch_sha256": sha(patch), "compiler": version,
                "projects": {p.get("path"): p.get("revision") for p in projects},
                "rt_built_in": True, "rt_abi": 3, "capabilities": 63,
                "original_operation_mask": 110, "device_tested": False,
                "original_apk_verified": False, "stock_boot_image_provided": False}
    (workspace / "kernel-input-verification.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("Full RT source staged for a custom kernel; no compilation or device action performed.", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.workspace.resolve(), args.cache.resolve())
