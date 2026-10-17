"""Contract regressions for ELF identification and kernel-derived vermagic."""
from pathlib import Path
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import verify_rt_diag as verify


class Tests(unittest.TestCase):
    def test_complete_report_keeps_incompatible_binary(self):
        # Controlled external command responses test report orchestration;
        # this fixture is not a compiled kernel module.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            report, kernel, config, bindir = [root / n for n in
                ("report", "kernel", "kernel_config", "bin")]
            for p in (report, kernel / "include/config", config / "out_dir", bindir):
                p.mkdir(parents=True)
            elf = bytearray(64)
            elf[:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<HHI", elf, 16, 1, 183, 1)
            (report / "rt_a17_diag.ko").write_bytes(elf)
            (report / "Module.symvers").write_text("0x12345678\tmodule_layout\tvmlinux\tEXPORT_SYMBOL\n")
            (kernel / "include/config/kernel.release").write_text(verify.TARGET + "\n")
            (config / "out_dir/.config").write_text('CONFIG_ARM64=y\nCONFIG_MODULES=y\n'
                'CONFIG_MODVERSIONS=y\nCONFIG_CFI_CLANG=y\nCONFIG_MODULE_SIG=y\n'
                'CONFIG_LOCALVERSION="-4k"\n')
            tool = '''#!/usr/bin/env python3
import os,sys
name=os.path.basename(sys.argv[0])
if name == "modinfo":
    fields={"name":os.environ.get("FIXTURE_NAME", "rt_a17_diag"), "version":os.environ.get("FIXTURE_VERSION", ""), "vermagic":os.environ["FIXTURE_MAGIC"]}
    print(fields.get(sys.argv[2], "") if "-F" in sys.argv else "name: rt_a17_diag")
elif name == "modprobe": print("0x12345678 module_layout")
else: print("controlled tool fixture")
'''
            for name in ("file", "readelf", "modinfo", "modprobe"):
                p = bindir / name
                p.write_text(tool)
                p.chmod(0o755)
            env = dict(os.environ, PATH=str(bindir) + os.pathsep + os.environ["PATH"],
                       FIXTURE_MAGIC=verify.TARGET + " modversions aarch64")
            command = [sys.executable, str(Path(verify.__file__)), "--report-dir", str(report),
                       "--kernel-artifact-dir", str(kernel), "--config-dir", str(config)]
            good = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
            result = json.loads((report / "rt-diag-verification.json").read_text())
            self.assertEqual(result["signature_state"], "unsigned")
            self.assertFalse(result["device_abi_verified"])
            env["FIXTURE_MAGIC"] = "6.1.112 modversions aarch64"
            bad = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(bad.returncode, 46, bad.stdout + bad.stderr)
            result = json.loads((report / "rt-diag-verification.json").read_text())
            self.assertFalse(result["build_checks_passed"])
            self.assertIn("vermagic", " ".join(result["errors"]))
            self.assertEqual((report / "rt_a17_diag.ko").read_bytes(), elf)
            (report / "rt_a17.ko").write_bytes(elf)
            env.update(FIXTURE_NAME="rt_a17", FIXTURE_VERSION="0.1",
                       FIXTURE_MAGIC=verify.TARGET + " modversions aarch64")
            port = subprocess.run(command + ["--module-name", "rt_a17"], env=env,
                                  capture_output=True, text=True)
            self.assertEqual(port.returncode, 0, port.stdout + port.stderr)
            result = json.loads((report / "rt-port-verification.json").read_text())
            self.assertFalse(result["original_rt_fully_implemented"])
            self.assertFalse(result["device_operations_tested"])
            self.assertEqual(result["implemented_original_opcodes"], ["0x801", "0x802", "0x803"])
            env.update(FIXTURE_VERSION="0.2")
            port2 = subprocess.run(command + ["--module-name", "rt_a17"], env=env,
                                   capture_output=True, text=True)
            self.assertEqual(port2.returncode, 0, port2.stdout + port2.stderr)
            result = json.loads((report / "rt-port-verification.json").read_text())
            self.assertEqual(result["port_abi"], 3)
            self.assertEqual(result["capabilities"], 0x3f)
            self.assertEqual(result["implemented_opcode_mask"], 0x6e)
            self.assertEqual(result["implemented_original_opcodes"],
                             ["0x801", "0x802", "0x803", "0x805", "0x806"])
            self.assertFalse(result["original_rt_fully_implemented"])

    def test_relocatable_aarch64_required(self):
        elf = bytearray(64)
        elf[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<HHI", elf, 16, 1, 183, 1)
        self.assertIn("ET_REL AArch64", verify.elf_header(elf))
        for elf_type, machine in ((2, 183), (1, 62), (3, 183)):
            struct.pack_into("<HHI", elf, 16, elf_type, machine, 1)
            with self.assertRaises(ValueError):
                verify.elf_header(elf)
        for data in (b"", b"ELF", bytes(64)):
            with self.assertRaises(ValueError):
                verify.elf_header(data)

    def test_config_and_full_vermagic(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "config"
            path.write_text('CONFIG_LOCALVERSION="-4k"\nCONFIG_SMP=y\n'
                            'CONFIG_PREEMPT_BUILD=y\nCONFIG_MODULE_UNLOAD=y\n'
                            'CONFIG_MODVERSIONS=y\n# CONFIG_MODULE_SIG_FORCE is not set\n')
            config = verify.read_config(path)
            self.assertEqual(config["CONFIG_MODULE_SIG_FORCE"], "n")
            self.assertEqual(config["CONFIG_LOCALVERSION"], "-4k")
            self.assertEqual(verify.expected_vermagic(config), verify.TARGET +
                             " SMP preempt mod_unload modversions aarch64")
            config["CONFIG_PREEMPT_BUILD"] = "n"
            config["CONFIG_PREEMPT_RT"] = "y"
            self.assertIn(" preempt_rt ", verify.expected_vermagic(config))


if __name__ == "__main__":
    unittest.main()
