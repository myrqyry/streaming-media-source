"""Regression tests for native SDK selection and package verification."""
import importlib.util
import pathlib
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("native", pathlib.Path(__file__).with_name("verify-native.py"))
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


class NativeTests(unittest.TestCase):
    def test_windows_selects_x64_not_x86(self):
        with tempfile.TemporaryDirectory() as tmp:
            obs = pathlib.Path(tmp)
            root = obs / ".deps"
            root.mkdir()
            for name in ("obs-deps-2026-07-15-x86", "obs-deps-2026-07-15-x64",
                         "obs-deps-qt6-2026-07-15-x64"):
                (root / name).mkdir()
            self.assertEqual(native.dependency_dirs(obs, "windows-x64"),
                             [root / "obs-deps-2026-07-15-x64", root / "obs-deps-qt6-2026-07-15-x64"])
            (root / "obs-deps-qt6-2026-07-15-x64").rmdir()
            with self.assertRaisesRegex(RuntimeError, "Qt"):
                native.dependency_dirs(obs, "windows-x64")

    def test_mac_architecture_checker_uses_input_first(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = pathlib.Path(tmp)
            bundle = stage / f"{native.MODULE}.plugin" / "Contents"
            binary = bundle / "MacOS" / native.MODULE
            notices = bundle / "Resources/licenses"
            binary.parent.mkdir(parents=True)
            notices.mkdir(parents=True)
            binary.write_bytes(b"fixture")
            (bundle / "Info.plist").write_bytes(native.plistlib.dumps({
                "CFBundleExecutable": native.MODULE, "CFBundlePackageType": "BNDL"}))
            for name, src in (("LICENSE", "LICENSE"), ("licensing.md", "docs/licensing.md")):
                (notices / name).write_bytes((native.ROOT / src).read_bytes())
            for arch in ("arm64", "x86_64"):
                with patch.object(native, "run") as run:
                    self.assertEqual(native.verify_stage(stage, f"macos-{arch}"), binary)
                    run.assert_any_call("lipo", binary, "-verify_arch", arch)


if __name__ == "__main__":
    unittest.main()
