"""Contract checks: kernel-family rejection, generated headers, ELF and selector."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile
import rt612


CONFIG = {k: "y" for k in ("CONFIG_ARM64", "CONFIG_64BIT", "CONFIG_MMU", "CONFIG_MODULES",
                           "CONFIG_PERF_EVENTS", "CONFIG_HAVE_HW_BREAKPOINT", "CONFIG_MODVERSIONS")}


class Tests(unittest.TestCase):
    def test_family_config_and_vermagic(self):
        for release in ("6.12.1", "6.12.38-android16-test-4k", "6.12.99+"):
            rt612.check_config(CONFIG, release)
        for release in ("6.120.1", "6.13.1", "6.12", "6.12.1\nevil", "6.12.x"):
            with self.assertRaises(ValueError): rt612.check_config(CONFIG, release)
        for key in CONFIG:
            if key == "CONFIG_MODVERSIONS": continue
            with self.assertRaises(ValueError): rt612.check_config(dict(CONFIG, **{key: "n"}), "6.12.1")
        self.assertEqual(rt612.vermagic(dict(CONFIG, CONFIG_SMP="y", CONFIG_PREEMPT_BUILD="y"), "6.12.1"),
                         "6.12.1 SMP preempt modversions aarch64")
        with self.assertRaises(ValueError):
            rt612.check_config(dict(CONFIG, CONFIG_RANDSTRUCT_FULL="y"), "6.12.1")

    def test_preflight_generated_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root / "kernel"; out = root / "out"
            source.mkdir(); (source / "Makefile").write_text("VERSION = 6\nPATCHLEVEL = 12\nSUBLEVEL = 1\n")
            (out / "include/config").mkdir(parents=True); (out / "include/generated").mkdir()
            (out / ".config").write_text("\n".join(f"{k}=y" for k in CONFIG))
            (out / "include/config/kernel.release").write_text("6.12.1\n")
            (out / "include/generated/utsrelease.h").write_text('#define UTS_RELEASE "6.12.1"\n')
            header = out / "include/generated/autoconf.h"
            header.write_text("\n".join(f"#define {k} 1" for k in CONFIG))
            symvers = out / "Module.symvers"
            symvers.write_text("".join(f"0x1234\t{name}\tvmlinux\tEXPORT_SYMBOL_GPL\n" for name in rt612.REQUIRED_EXPORTS))
            self.assertEqual(rt612.preflight(source, out)[1], "6.12.1")
            (source / "Makefile").write_text("VERSION = 6\nPATCHLEVEL = 12\nSUBLEVEL = 2\n")
            with self.assertRaisesRegex(ValueError, "SUBLEVEL"):
                rt612.preflight(source, out)
            (source / "Makefile").write_text("VERSION = 6\nPATCHLEVEL = 12\nSUBLEVEL = 1\n")
            header.write_text(header.read_text().replace("#define CONFIG_MODVERSIONS 1", ""))
            with self.assertRaisesRegex(ValueError, "Stale autoconf"):
                rt612.preflight(source, out)

    def test_selector_exact_release_and_integrity(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); kit = root / "RTDriver-A17/multikernel"; kit.mkdir(parents=True)
            script = kit / "check_kernel.sh"; shutil.copyfile(rt612.HERE / script.name, script)
            bindir = root / "bin"; bindir.mkdir()
            uname = bindir / "uname"
            def run(release, arch="aarch64"):
                uname.write_text(f'#!/bin/sh\ncase "$1" in -r) echo "{release}";; -m) echo "{arch}";; esac\n')
                uname.chmod(0o755)
                env = dict(os.environ, PATH=str(bindir) + ":/usr/bin:/bin")
                return subprocess.run(["sh", str(script)], env=env, capture_output=True, text=True)
            self.assertEqual(run("6.11.1").returncode, 2)
            self.assertEqual(run("6.12.1", "x86_64").returncode, 3)
            self.assertEqual(run("6.12.1").returncode, 7)
            target = root / "targets/a17"; target.mkdir(parents=True)
            (target / "architecture").write_text("aarch64\n")
            (target / "kernel.release").write_text("6.12.38-a17\n")
            (target / "rt_a17.ko").write_bytes(b"checksum fixture, not a module")
            (target / "SHA256SUMS").write_text(rt612.digest((target / "rt_a17.ko").read_bytes()) + "  rt_a17.ko\n")
            self.assertEqual(run("6.12.38-other").returncode, 7)
            candidate = run("6.12.38-a17")
            self.assertEqual(candidate.returncode, 6, candidate.stdout + candidate.stderr)
            self.assertIn("device_compatible_verified=false", candidate.stdout)
            (target / "rt_a17.ko").write_bytes(b"corrupted")
            self.assertEqual(run("6.12.38-a17").returncode, 4)

    def test_archive_digest_and_elf_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); bad = root / "bad.zip"; bad.write_bytes(b"not the pinned artifact")
            import argparse
            with self.assertRaisesRegex(ValueError, "digest"):
                rt612.bundle(argparse.Namespace(artifact_zip=bad, profile="samsung-a17-czi4", output=root / "result"))
            self.assertFalse((root / "result").exists())
        for data in (b"", bytes(64), b"\x7fELF\x01\x01" + bytes(58)):
            with self.assertRaises(ValueError): rt612.sections(data)
        index = json.loads((rt612.HERE / "profiles.json").read_text())
        self.assertFalse(index["single_universal_binary"])
        self.assertTrue(all(not p["device_tested"] for p in index["profiles"]))


@unittest.skipUnless(os.environ.get("RT612_REAL_ARTIFACT"), "Requires the actual pinned Actions artifact")
class ActualArtifactTests(unittest.TestCase):
    def setUp(self):
        with zipfile.ZipFile(os.environ["RT612_REAL_ARTIFACT"]) as archive:
            self.module = archive.read("rt_a17.ko")
            self.symvers = archive.read("Module.symvers")
            self.config = rt612.config_values(archive.read("kernel.config").decode())
            self.release = archive.read("kernel.release").decode().strip()

    def test_actual_imports_and_crc_tables(self):
        result = rt612.inspect_module(self.module, self.symvers, self.config, self.release, "rt_a17")
        self.assertEqual(result["crc_matches"], 60)
        self.assertEqual(result["imports"], 59)
        self.assertFalse(result["device_compatible_verified"])

    def test_reject_wrong_kernel_crc_and_release(self):
        rows = self.symvers.decode().splitlines()
        for i, line in enumerate(rows):
            words = line.split()
            if words[1] == "modify_user_hw_breakpoint":
                words[0] = hex(int(words[0], 16) ^ 1); rows[i] = "\t".join(words); break
        else: self.fail("Expected exported BP symbol")
        with self.assertRaisesRegex(ValueError, "Target CRC mismatch"):
            rt612.inspect_module(self.module, "\n".join(rows).encode(), self.config, self.release, "rt_a17")
        with self.assertRaisesRegex(ValueError, "vermagic mismatch"):
            rt612.inspect_module(self.module, self.symvers, self.config, self.release + "-other", "rt_a17")

    def test_reject_disagreeing_extended_crc_and_forced_signature(self):
        section = rt612.sections(self.module)[0]["__version_ext_crcs"][0]
        broken = bytearray(self.module); broken[section[4]] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC tables disagree"):
            rt612.inspect_module(bytes(broken), self.symvers, self.config, self.release, "rt_a17")
        with self.assertRaisesRegex(ValueError, "MODULE_SIG_FORCE"):
            rt612.inspect_module(self.module, self.symvers, dict(self.config, CONFIG_MODULE_SIG_FORCE="y"),
                                self.release, "rt_a17")


if __name__ == "__main__": unittest.main()
