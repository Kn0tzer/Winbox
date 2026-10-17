import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('mono', Path(__file__).parents[1] / 'rootfs64/prepare-mono32.py')
mono = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mono)


class MonoIntegrity(unittest.TestCase):
    def test_checks_assemblies_and_extra_files_not_only_runtime_dlls(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {
                'mono/mono-2.0/bin/libmono-2.0-x86.dll': b'x86',
                'mono/mono-2.0/bin/libmono-2.0-x86_64.dll': b'x64',
                'mono/mono-2.0/lib/mono/4.5/mscorlib.dll': b'core',
                'mono/mono-2.0/lib/FNA3D-x86.dll': b'graphics',
            }
            tree = hashlib.sha256()
            for name, data in sorted(files.items()):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                tree.update((name + '\0' + hashlib.sha256(data).hexdigest() + '\n').encode())
            mono.LIBRARIES = {Path(name).name: hashlib.sha256(data).hexdigest()
                              for name, data in files.items() if '/bin/' in name}
            mono.TREE_SHA256 = tree.hexdigest()
            mono.check(root)
            assembly = root / 'mono/mono-2.0/lib/FNA3D-x86.dll'
            assembly.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'differs from the pinned'):
                mono.check(root)
            assembly.write_bytes(b'graphics')
            (root / 'unexpected.dll').write_bytes(b'extra')
            with self.assertRaisesRegex(ValueError, 'differs from the pinned'):
                mono.check(root)
            (root / 'unexpected.dll').unlink()
            (root / 'unsafe.dll').symlink_to(assembly)
            with self.assertRaisesRegex(ValueError, 'symlinks'):
                mono.check(root)


if __name__ == '__main__':
    unittest.main()
