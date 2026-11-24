"""Regression tests for native SDK selection and package verification."""
import importlib.util
import pathlib
import tempfile
import unittest

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
        binary = pathlib.Path("stage/obs-streaming-media.plugin/Contents/MacOS/obs-streaming-media")
        for arch in ("arm64", "x86_64"):
            self.assertEqual(native.mac_arch_command(binary, f"macos-{arch}"),
                             ("lipo", binary, "-verify_arch", arch))


if __name__ == "__main__":
    unittest.main()
