"""Regression tests for native SDK selection and package verification."""
import importlib.util
import pathlib
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("native", pathlib.Path(__file__).with_name("verify-native.py"))
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


class NativeTests(unittest.TestCase):
    def test_native_report_respects_shared_minimum_and_rejects_bad_results(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = pathlib.Path(tmp) / "ctest.xml"
            def write(count, extra="", status="run"):
                cases = "".join(f'<testcase name="test-{i}" status="{status}">{extra}</testcase>'
                                for i in range(count))
                report.write_text(f"<testsuite>{cases}</testsuite>", encoding="utf-8")
            for count in (10, 11):
                write(count)
                self.assertEqual(native.verify_test_report(report, 10), count)
            for count, extra, status in ((9, "", "run"), (0, "", "run"),
                (10, "<skipped/>", "run"), (10, "<failure/>", "run"),
                (10, "<error/>", "run"), (10, "", "notrun"), (10, "", "disabled")):
                with self.subTest(count=count, extra=extra, status=status):
                    write(count, extra, status)
                    with self.assertRaises(RuntimeError):
                        native.verify_test_report(report, 10)
            write(10)
            for invalid in (0, -1):
                with self.assertRaises(ValueError):
                    native.verify_test_report(report, invalid)

    def test_obs33_pin_and_dependency_selection(self):
        pin = native.sdk_pin("33.0.3")
        self.assertEqual(pin["dependencies"], "2026-10-23")
        self.assertEqual(pin["websocket_revision"], "e28f84975984340e84fc4879cfc05741cc0afde1")
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            native.sdk_pin("33.1.0")
        with tempfile.TemporaryDirectory() as tmp:
            obs = pathlib.Path(tmp)
            root = obs / ".deps"
            root.mkdir()
            for arch in ("x64", "universal"):
                for prefix in ("obs-deps", "obs-deps-qt6"):
                    (root / f"{prefix}-2026-10-23-{arch}").mkdir()
            for target, arch in (("windows-x64", "x64"), ("macos-arm64", "universal"),
                                 ("macos-x86_64", "universal")):
                self.assertEqual(native.dependency_dirs(obs, target, "33.0.3"),
                    (root / f"obs-deps-2026-10-23-{arch}", root / f"obs-deps-qt6-2026-10-23-{arch}"))

    def test_obs33_windows_does_not_apply_obsolete_patch(self):
        with tempfile.TemporaryDirectory() as tmp:
            obs = pathlib.Path(tmp)
            (obs / "libobs").mkdir()
            source = obs / "libobs/obs-properties.c"
            corrected = b"return data->items.array[idx].b : false;"
            source.write_bytes(corrected)
            native.patch_obs_sdk(obs, "windows-x64", "33.0.3")
            self.assertEqual(source.read_bytes(), corrected)
            source.write_bytes(b"unexpected source")
            with self.assertRaisesRegex(RuntimeError, "Unexpected"):
                native.patch_obs_sdk(obs, "windows-x64", "33.0.3")
            source.write_bytes(b"return data->items.array[idx].d : false;")
            native.patch_obs_sdk(obs, "windows-x64", "32.2.0")
            self.assertEqual(source.read_bytes(), corrected)

    def test_version_specific_module_paths(self):
        for target, extension in (("windows-x64", "dll"), ("linux-x86_64", "so")):
            name = f"obs-streaming-media.{extension}"
            self.assertEqual(native.module_relative_path(target, "32.2.0"),
                pathlib.Path("obs-streaming-media/bin/64bit") / name)
            self.assertEqual(native.module_relative_path(target, "33.0.3"),
                pathlib.Path("obs-streaming-media") / name)
        self.assertEqual(native.module_relative_path("macos-arm64", "33.0.3"),
            pathlib.Path("obs-streaming-media.plugin/Contents/MacOS/obs-streaming-media"))

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
