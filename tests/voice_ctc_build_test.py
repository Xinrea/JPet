#!/usr/bin/env python3
"""Offline regression tests for the pinned-source preparation helper."""
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "build_hotwords", Path(__file__).resolve().parents[1] / "build-scripts/build_sensevoice_hotwords.py")
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


class PrepareSourceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name).resolve()
        self.archive = self.directory / f"sherpa-onnx-v{build.VERSION}.tar.gz"
        self.patch_file = self.directory / "test.patch"
        self.patch_file.write_text("--- a/example.txt\n+++ b/example.txt\n@@ -1 +1 @@\n-before\n+after\n")
        self.source_name = f"sherpa-onnx-{build.VERSION}"

    def write_archive(self, traversal=False):
        with tarfile.open(self.archive, "w:gz") as tar:
            data = b"before\n"
            info = tarfile.TarInfo("../escaped.txt" if traversal else f"{self.source_name}/example.txt")
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
            # The actual pinned upstream archive has example-only symlinks,
            # including absolute links. They must be skipped, never followed.
            link = tarfile.TarInfo(f"{self.source_name}/example-link")
            link.type = tarfile.SYMTYPE
            link.linkname = "/nonexistent/upstream-authors-machine"
            tar.addfile(link)

    def prepare(self):
        with patch.object(build, "PATCH", self.patch_file), patch.object(
                build, "ARCHIVE_SHA256", build.sha256(self.archive)):
            return build.prepare_source(self.directory)

    def test_applies_patch_skips_symlinks_and_reuses_stamped_source(self):
        self.write_archive()
        source, stamp = self.prepare()
        self.assertEqual((source / "example.txt").read_text(), "after\n")
        self.assertFalse((source / "example-link").is_symlink())
        self.assertEqual(json.loads((source / ".jpet-source.json").read_text()), stamp)
        self.assertEqual(self.prepare(), (source, stamp))

    def test_changed_patch_refuses_to_overwrite_existing_source(self):
        self.write_archive()
        source, _ = self.prepare()
        self.patch_file.write_text(self.patch_file.read_text() + "\n")
        with self.assertRaisesRegex(RuntimeError, "fresh --build-dir"):
            self.prepare()
        self.assertEqual((source / "example.txt").read_text(), "after\n")

    def test_checksum_mismatch_fails_before_extraction(self):
        self.write_archive()
        with patch.object(build, "ARCHIVE_SHA256", "0" * 64):
            with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
                build.prepare_source(self.directory)
        self.assertFalse((self.directory / "source").exists())

    def test_path_traversal_rejected_before_extraction(self):
        self.write_archive(traversal=True)
        with self.assertRaisesRegex(RuntimeError, "Unsafe archive member"):
            self.prepare()
        self.assertFalse((self.directory / "escaped.txt").exists())
        self.assertFalse((self.directory / "source").exists())


if __name__ == "__main__":
    unittest.main()
