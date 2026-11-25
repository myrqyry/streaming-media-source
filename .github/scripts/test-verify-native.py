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
                             (root / "obs-deps-2026-07-15-x64", root / "obs-deps-qt6-2026-07-15-x64"))
            (root / "obs-deps-qt6-2026-07-15-x64").rmdir()
            with self.assertRaisesRegex(RuntimeError, "Qt"):
                native.dependency_dirs(obs, "windows-x64")

    def test_macos_selects_pinned_universal_pair(self):
        with tempfile.TemporaryDirectory() as tmp:
            obs = pathlib.Path(tmp)
            root = obs / ".deps"
            root.mkdir()
            for name in ("obs-deps-2026-07-15-universal",
                         "obs-deps-qt6-2026-07-15-universal",
                         "obs-deps-2026-07-15-arm64"):
                (root / name).mkdir()
            expected = (root / "obs-deps-2026-07-15-universal",
                        root / "obs-deps-qt6-2026-07-15-universal")
            for arch in ("arm64", "x86_64"):
                self.assertEqual(native.dependency_dirs(obs, f"macos-{arch}"), expected)
            with self.assertRaisesRegex(RuntimeError, "Unsupported"):
                native.dependency_dirs(obs, "unknown")

    def test_windows_headless_backend_is_installed(self):
        with tempfile.TemporaryDirectory() as tmp:
            qt = pathlib.Path(tmp)
            platforms = qt / "plugins" / "platforms"
            platforms.mkdir(parents=True)
            (platforms / "qminimal.dll").touch()
            self.assertEqual(native.qt_test_platform(qt, "windows-x64"), "minimal")
            (platforms / "qminimal.dll").unlink()
            with self.assertRaisesRegex(RuntimeError, "qminimal.dll"):
                native.qt_test_platform(qt, "windows-x64")
            for arch in ("arm64", "x86_64"):
                self.assertEqual(native.qt_test_platform(qt, f"macos-{arch}"), "offscreen")

    def test_mac_architecture_checker_uses_input_first(self):
        binary = pathlib.Path("stage/obs-streaming-media.plugin/Contents/MacOS/obs-streaming-media")
        for arch in ("arm64", "x86_64"):
            self.assertEqual(native.mac_arch_command(binary, f"macos-{arch}"),
                             ("lipo", binary, "-verify_arch", arch))


if __name__ == "__main__":
    unittest.main()
