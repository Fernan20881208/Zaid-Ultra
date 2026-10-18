"""Integrity, explicit build gating, and capability-related input contracts."""
import hashlib
import json
from pathlib import Path
import unittest
import prepare_gki_ddk as prepare
import resolve_gki_build_request as request


class Tests(unittest.TestCase):
    def test_integrity_rejects_replaced_artifact(self):
        data = b"controlled integrity fixture, not a kernel"
        spec = {"size": len(data), "md5": hashlib.md5(data).hexdigest(),
                "sha256": hashlib.sha256(data).hexdigest()}
        prepare.verify(data, spec)
        with self.assertRaises(ValueError):
            prepare.verify(data + b"x", spec)
        with self.assertRaises(ValueError):
            prepare.verify(data, dict(spec, sha256="0" * 64))

    def test_request_requires_one_new_file_and_matching_parent(self):
        changes = [["A", "RTDriver-A17/build-requests/gki-taipei-14987696.json"]]
        body = {"source_commit": "parent", "build_id": "14987696", "mode": "modules"}
        self.assertEqual(request.resolve(changes, "parent", body), "modules")
        for change, req in (([], body), (changes * 2, body),
                            ([["M", changes[0][1]]], body),
                            (changes, dict(body, source_commit="other")),
                            (changes, dict(body, build_id="other")),
                            (changes, dict(body, mode="kernel"))):
            with self.assertRaises(ValueError):
                request.resolve(change, "parent", req)

    def test_critical_inputs_have_sha256_pins(self):
        lock = json.loads(prepare.LOCK.read_text())
        for name in ("init_ddk.zip", "manifest_14987696.xml", "kernel_aarch64_Module.symvers",
                     "kernel_aarch64_dot_config", "kernel_aarch64_filegroup_decl.tar.gz",
                     "kernel_aarch64_ddk_headers_archive.tar.gz", "build.config.constants"):
            self.assertEqual(len(lock["files"][name]["sha256"]), 64, name)
        self.assertEqual(lock["files"]["kernel_aarch64_dot_config"]["sha256"],
                         lock["device_config_sha256"])
        self.assertIn("RT_A17_MEMORY_ONLY", (prepare.ROOT / "gki/BUILD.bazel").read_text())


if __name__ == "__main__":
    unittest.main()
