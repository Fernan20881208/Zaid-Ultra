"""Protect the full-build gate and prevent silent kernel security-config drift."""
from pathlib import Path
import tempfile
import unittest

from resolve_gki_kernel_request import resolve
from verify_gki_kernel import compare_config


class KernelGateTest(unittest.TestCase):
    def test_one_explicit_request_and_exact_parent_required(self):
        changes = [["A", "RTDriver-A17/kernel-requests/taipei-v1.json"]]
        req = {"source_commit": "a" * 40, "build_id": "14987696",
               "mode": "kernel", "rt_integration": "builtin"}
        self.assertEqual(resolve(changes, "a" * 40, req), "kernel")
        for invalid in (changes + [["M", "RTDriver-A17/rt_port/runtime/rt_main.c"]],
                        [["M", changes[0][1]]], [["A", "RTDriver-A17/build-requests/gki-v1.json"]]):
            with self.assertRaises(ValueError):
                resolve(invalid, "a" * 40, req)
        with self.assertRaises(ValueError):
            resolve(changes, "b" * 40, req)

    def test_custom_config_permits_only_rt_and_honest_release_identity(self):
        baseline = ('CONFIG_LOCALVERSION="-4k"\nCONFIG_MODULE_SIG=y\n'
                    'CONFIG_MODULE_SIG_PROTECT=y\nCONFIG_CFI_CLANG=y\n'
                    'CONFIG_TRIM_UNUSED_KSYMS=y\n')
        custom = baseline.replace('"-4k"', '"-4k-rt-taipei"') + 'CONFIG_RT_A17=y\n'
        with tempfile.TemporaryDirectory() as tmp:
            before, after = Path(tmp) / "stock", Path(tmp) / "custom"
            before.write_text(baseline)
            after.write_text(custom)
            self.assertEqual(set(compare_config(before, after)), {"CONFIG_LOCALVERSION", "CONFIG_RT_A17"})
            for symbol in ("CONFIG_MODULE_SIG", "CONFIG_MODULE_SIG_PROTECT",
                           "CONFIG_CFI_CLANG", "CONFIG_TRIM_UNUSED_KSYMS"):
                after.write_text(custom.replace(symbol + "=y", "# " + symbol + " is not set"))
                with self.assertRaises(ValueError):
                    compare_config(before, after)
            after.write_text(custom.replace('"-4k-rt-taipei"', '"-4k"'))
            with self.assertRaises(ValueError):
                compare_config(before, after)


if __name__ == "__main__":
    unittest.main()
