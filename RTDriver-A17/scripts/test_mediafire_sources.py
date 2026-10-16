#!/usr/bin/env python3
"""Offline unit tests. No MediaFire network request is required."""
import base64
import hashlib
import io
from pathlib import Path
import tempfile
import unittest
import zipfile
import download_mediafire_sources as mf


class Tests(unittest.TestCase):
    def test_html_anchor_scrambled(self):
        direct = "https://download1234.mediafire.com/abc123/filename.zip"
        encoded = base64.b64encode(direct.encode("utf-8")).decode("ascii")
        page = f'<a id="downloadButton" data-scrambled-url="{encoded}" href="#">Download</a>'
        self.assertEqual(mf.extract_download_url(page), direct)

    def test_html_anchor_href(self):
        direct = "https://download22.mediafire.com/test/example.zip?a=1&b=2"
        page = '<a id="downloadButton" href="' + direct.replace("&", "&amp;") + '">Download</a>'
        self.assertEqual(mf.extract_download_url(page), direct)

    def test_reject_invalid_and_wrong_hosts(self):
        for html in (
            "<title>Just a moment...</title>",
            '<a id="downloadButton" href="javascript:void(0)">Download</a>',
            '<a id="downloadButton" href="https://example.com/file.zip">Download</a>',
        ):
            with self.assertRaises(RuntimeError):
                mf.extract_download_url(html)

    def test_patch_extract_and_verify(self):
        payload = b"patch-archive-fixture"
        with tempfile.TemporaryDirectory() as td:
            bundle = Path(td) / "bundle.zip"
            patch = Path(td) / "patch.zip"
            archive_bytes = io.BytesIO()
            with zipfile.ZipFile(archive_bytes, "w") as z:
                z.writestr("file.txt", payload)
            patch_data = archive_bytes.getvalue()
            # Substitute fixture SHA inside the imported module only.
            saved = mf.PATCH_SHA256
            try:
                mf.PATCH_SHA256 = hashlib.sha256(patch_data).hexdigest()
                with zipfile.ZipFile(bundle, "w") as z:
                    z.writestr(mf.PATCH_NAME, patch_data)
                mf.extract_patch(bundle, patch)
                self.assertEqual(patch.read_bytes(), patch_data)
            finally:
                mf.PATCH_SHA256 = saved

    def test_reject_other_file(self):
        with tempfile.TemporaryDirectory() as td:
            bundle = Path(td) / "bundle.zip"
            with zipfile.ZipFile(bundle, "w") as z:
                z.writestr("unrelated.zip", b"junk")
            with self.assertRaises(RuntimeError):
                mf.extract_patch(bundle, Path(td) / mf.PATCH_NAME)


if __name__ == "__main__":
    unittest.main()
