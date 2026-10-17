"""Contract regressions for ELF identification and kernel-derived vermagic."""
from pathlib import Path
import struct
import tempfile
import unittest
import verify_rt_diag as verify


class Tests(unittest.TestCase):
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
