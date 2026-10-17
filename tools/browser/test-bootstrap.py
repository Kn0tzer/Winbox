#!/usr/bin/env python3
"""Regression checks for verified, all-or-nothing runtime asset restoration."""
import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest

from bootstrap import restore


class RestoreTests(unittest.TestCase):
    def bundle(self, folder, entries):
        archive = folder / "assets.tar"
        files = []
        with tarfile.open(archive, "w") as bundle:
            for name, data in entries:
                member = tarfile.TarInfo(name)
                member.size = len(data)
                bundle.addfile(member, io.BytesIO(data))
                files.append({"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        manifest = {
            "archive": {"bytes": archive.stat().st_size, "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()},
            "files": files,
        }
        return archive, manifest

    def test_valid_bundle_restores_exact_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            archive, manifest = self.bundle(root, [("web/runtime/test.zip", b"current runtime")])
            restore(manifest, archive, root / "checkout")
            self.assertEqual((root / "checkout/web/runtime/test.zip").read_bytes(), b"current runtime")
            self.assertEqual((root / "checkout/web/runtime/test.zip").stat().st_mode & 0o777, 0o644)

    def test_corrupt_download_keeps_cached_assets(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            target = root / "checkout/asset"
            target.parent.mkdir()
            target.write_bytes(b"cached")
            archive, manifest = self.bundle(root, [("asset", b"replacement")])
            archive.write_bytes(archive.read_bytes()[:-1])
            with self.assertRaisesRegex(ValueError, "archive size/SHA256 mismatch"):
                restore(manifest, archive, target.parent)
            self.assertEqual(target.read_bytes(), b"cached")

    def test_bad_later_entry_publishes_none_of_the_bundle(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            target = root / "checkout/first"
            target.parent.mkdir()
            target.write_bytes(b"cached")
            archive, manifest = self.bundle(root, [("first", b"replacement"), ("second", b"corrupt")])
            manifest["files"][1]["sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "asset size/SHA256 mismatch"):
                restore(manifest, archive, target.parent)
            self.assertEqual(target.read_bytes(), b"cached")
            self.assertFalse((target.parent / "second").exists())

    def test_path_traversal_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            archive, manifest = self.bundle(root, [("../escape", b"unexpected")])
            with self.assertRaisesRegex(ValueError, "unsafe archive entry"):
                restore(manifest, archive, root / "checkout")
            self.assertFalse((root / "escape").exists())


if __name__ == "__main__":
    unittest.main()
