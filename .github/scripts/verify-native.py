"""Build and verify a version-pinned OBS module-only package on a native Windows/macOS host."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import tarfile
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

DEFAULT_VERSION = "32.2.0"
PINS = json.loads(Path(__file__).with_name("obs-sdk-pins.json").read_text(encoding="utf-8"))
MODULE = "obs-streaming-media"
ROOT = Path(__file__).resolve().parents[2]


def sdk_pin(version):
    if version not in PINS:
        raise ValueError(f"Unsupported OBS SDK version: {version}")
    return PINS[version]


def module_relative_path(target, version):
    sdk_pin(version)
    if target in ("macos-arm64", "macos-x86_64"):
        return Path(f"{MODULE}.plugin/Contents/MacOS/{MODULE}")
    if target not in ("windows-x64", "linux-x86_64"):
        raise ValueError(f"Unsupported target: {target}")
    suffix = "dll" if target == "windows-x64" else "so"
    root = Path(MODULE)
    if version == DEFAULT_VERSION:
        root /= "bin/64bit"
    return root / f"{MODULE}.{suffix}"


def run(*args, env=None, cwd=None):
    print("+", *args, flush=True)
    subprocess.run([str(a) for a in args], check=True, env=env, cwd=cwd)


def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def download(url, path, expected):
    request = urllib.request.Request(url, headers={"User-Agent": "streaming-media-source-native-ci"})
    with urllib.request.urlopen(request, timeout=120) as src, path.open("wb") as dst:
        shutil.copyfileobj(src, dst)
    if digest(path) != expected:
        raise RuntimeError(f"SHA-256 mismatch: {path.name}")


def source_archive(work, version):
    pin = sdk_pin(version)
    archive = work / "obs.tar.gz"
    download(f"https://github.com/obsproject/obs-studio/archive/refs/tags/{version}.tar.gz",
             archive, pin["source_sha256"])
    with tarfile.open(archive) as tar:
        # The source archive contains symlinks to unused upstream shell
        # scripts. Windows need not have symlink privileges to extract them.
        members = [m for m in tar.getmembers() if not m.issym() and not m.islnk()]
        tar.extractall(work, members=members, filter="data")
    return work / f"obs-studio-{version}"


def patch_once(path, old, new):
    """Apply only an exact, reviewed change to the checksum-pinned SDK."""
    data = path.read_bytes()
    old, new = old.encode(), new.encode()
    if data.count(old) != 1:
        raise RuntimeError(f"Unexpected OBS source: {path}; expected exactly one occurrence of {old!r}")
    path.write_bytes(data.replace(old, new, 1))


def patch_obs_sdk(obs, target, version=DEFAULT_VERSION):
    sdk_pin(version)
    if target == "windows-x64":
        # OBS 32.2.0's boolean list getter reads the double union member,
        # although add_item/insert_item store the boolean member.
        path = obs / "libobs/obs-properties.c"
        if version == DEFAULT_VERSION:
            patch_once(path, "data->items.array[idx].d : false;",
                       "data->items.array[idx].b : false;")
        elif path.read_bytes().count(b"data->items.array[idx].b : false;") != 1:
            raise RuntimeError(f"Unexpected OBS {version} source: {path}")
    elif target.startswith("macos-"):
        # The installed framework's Resources symlink causes CMake's
        # generated _IMPORT_PREFIX to resolve inside libobs.framework.
        # Export package metadata outside the framework, leaving its
        # actual binary and public headers in their standard locations.
        patch_once(obs / "cmake/common/helpers_common.cmake",
                   'set(package_destination "Frameworks/${target}.framework/Resources/cmake")',
                   'set(package_destination "${OBS_CMAKE_DESTINATION}/${target}")')


def config_dir(root, package):
    matches = list(root.rglob(f"{package}Config.cmake"))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one {package} config in {root}, found {matches}")
    return matches[0].parent


def dependency_dirs(obs, target, version=DEFAULT_VERSION):
    release = sdk_pin(version)["dependencies"]
    # OBS also builds/downloads x86 helper dependencies on Windows.
    # Only the native x64 (or universal macOS) pair belongs on the
    # plugin's CMake prefix and runtime library search paths.
    if target == "windows-x64":
        arch = "x64"
    elif target in ("macos-arm64", "macos-x86_64"):
        # Both architectures use the pinned universal archive.
        arch = "universal"
    else:
        raise RuntimeError(f"Unsupported dependency target: {target}")
    base = obs / ".deps"
    deps = (base / f"obs-deps-{release}-{arch}",
            base / f"obs-deps-qt6-{release}-{arch}")
    for path, label in zip(deps, ("OBS", "Qt")):
        if not path.is_dir():
            raise RuntimeError(f"Missing pinned {label} dependency directory: {path}")
    return deps


def qt_test_platform(qt, target):
    if target == "windows-x64":
        # The pinned Windows Qt archive includes minimal/windows/direct2d,
        # not offscreen. Fail before launching a GUI executable if absent.
        plugin = qt / "plugins" / "platforms" / "qminimal.dll"
        if not plugin.is_file():
            raise RuntimeError(f"Missing headless Qt platform plugin: {plugin}")
        return "minimal"
    return "offscreen"


def native_config():
    host = platform.system()
    arch = platform.machine().lower()
    if host == "Windows" and arch in ("amd64", "x86_64"):
        return "windows-x64", ["-G", "Visual Studio 18 2026", "-A", "x64,version=10.0.26100.0"]
    if host == "Darwin" and arch in ("arm64", "x86_64"):
        return f"macos-{arch}", ["-G", "Xcode", f"-DCMAKE_OSX_ARCHITECTURES={arch}",
                                 "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0",
                                 "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO"]
    raise RuntimeError(f"Unsupported native host: {host}/{arch}")


def mac_arch_command(binary, target):
    arch = target.removeprefix("macos-")
    return ("lipo", binary, "-verify_arch", arch)


def verify_stage(stage, target, version=DEFAULT_VERSION):
    if target == "windows-x64":
        binary = stage / module_relative_path(target, version)
        notices = stage / MODULE / "data/licenses"
        expected = {binary, notices / "LICENSE", notices / "licensing.md"}
        actual = {p for p in stage.rglob("*") if p.is_file()}
        if actual != expected:
            raise RuntimeError(f"Unexpected Windows install payload: {actual ^ expected}")
        # PE machine 0x8664 is AMD64.
        with binary.open("rb") as f:
            if f.read(2) != b"MZ":
                raise RuntimeError("Not a PE image")
            f.seek(0x3c)
            offset = int.from_bytes(f.read(4), "little")
            f.seek(offset)
            if f.read(4) != b"PE\0\0" or int.from_bytes(f.read(2), "little") != 0x8664:
                raise RuntimeError("Not a Windows x64 module")
    else:
        bundle = stage / f"{MODULE}.plugin"
        binary = bundle / "Contents/MacOS" / MODULE
        notices = bundle / "Contents/Resources/licenses"
        plist = bundle / "Contents/Info.plist"
        with plist.open("rb") as f:
            info = plistlib.load(f)
        if info.get("CFBundleExecutable") != MODULE or info.get("CFBundlePackageType") != "BNDL":
            raise RuntimeError("Invalid OBS plugin bundle metadata")
        expected = {binary, plist, notices / "LICENSE", notices / "licensing.md"}
        actual = {p for p in stage.rglob("*") if p.is_file()}
        if actual != expected:
            raise RuntimeError(f"Unexpected macOS install payload: {actual ^ expected}")
        run(*mac_arch_command(binary, target))
        run("otool", "-L", binary)
    for path, source in [(notices / "LICENSE", ROOT / "LICENSE"),
                         (notices / "licensing.md", ROOT / "docs/licensing.md")]:
        if path.read_bytes() != source.read_bytes():
            raise RuntimeError(f"Missing or altered notice: {path}")
    return binary


def verify_test_report(report, minimum_tests):
    """Apply the shared CI minimum without rejecting additional passing tests."""
    if minimum_tests < 1:
        raise ValueError("minimum-tests must be positive")
    cases = ET.parse(report).findall(".//testcase")
    if len(cases) < minimum_tests or any(
        c.find("skipped") is not None or c.find("failure") is not None
        or c.find("error") is not None or c.get("status") in ("notrun", "disabled")
        for c in cases
    ):
        raise RuntimeError(f"Native regression suite incomplete or failed: {len(cases)} results, minimum {minimum_tests}")
    return len(cases)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("work", type=Path)
    p.add_argument("--obs-version", choices=sorted(PINS), default=DEFAULT_VERSION)
    p.add_argument("--minimum-tests", type=int, default=os.environ.get("MINIMUM_TESTS", "10"))
    args = p.parse_args()
    if args.minimum_tests < 1:
        p.error("minimum-tests must be positive")
    version = args.obs_version
    pin = sdk_pin(version)
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    target, generator = native_config()
    obs = source_archive(work, version)
    patch_obs_sdk(obs, target, version)
    sdk = work / "sdk"
    obs_build = work / "obs-build"
    # OBS's source archive pins the matching obs-deps and Qt hashes in
    # CMakePresets.json. Let the upstream dependency bootstrap enforce them.
    run("cmake", "-S", obs, "-B", obs_build, *generator,
        "-DOBS_CMAKE_VERSION:STRING=3.0.0", f"-DOBS_VERSION_OVERRIDE:STRING={version}",
        "-DENABLE_PLUGINS=OFF", "-DENABLE_FRONTEND=OFF",
        "-DENABLE_BROWSER=OFF", "-DENABLE_SCRIPTING=OFF")
    run("cmake", "--build", obs_build, "--config", "Release",
        "--target", "obs-frontend-api", "--parallel", "4")
    run("cmake", "--install", obs_build, "--config", "Release",
        "--component", "Development", "--prefix", sdk)
    obs_deps, qt_deps = dependency_dirs(obs, target, version)
    deps = (obs_deps, qt_deps)
    qpa = qt_test_platform(qt_deps, target)
    header = work / "obs-websocket-api.h"
    download(f"https://raw.githubusercontent.com/obsproject/obs-websocket/{pin['websocket_revision']}/lib/obs-websocket-api.h",
             header, pin["websocket_sha256"])
    build = work / "build"
    prefixes = ";".join(p.as_posix() for p in [sdk, *deps])
    run("cmake", "-S", ROOT, "-B", build, *generator,
        f"-DCMAKE_PREFIX_PATH={prefixes}",
        f"-Dlibobs_DIR={config_dir(sdk, 'libobs').as_posix()}",
        f"-Dobs-frontend-api_DIR={config_dir(sdk, 'obs-frontend-api').as_posix()}",
        f"-DOBS_WEBSOCKET_INCLUDE_DIR={work.as_posix()}", "-DSTREAMING_MEDIA_RUNTIME_DIR=",
        f"-DSTREAMING_MEDIA_PACKAGE_LAYOUT={'modern' if version != DEFAULT_VERSION else 'legacy'}")
    run("cmake", "--build", build, "--config", "Release", "--parallel", "4")
    env = os.environ.copy()
    env["QT_PLUGIN_PATH"] = str(qt_deps / "plugins")
    env["QT_QPA_PLATFORM"] = qpa
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    if target == "windows-x64":
        env["QT_DEBUG_PLUGINS"] = "1"
    if target == "windows-x64":
        env["PATH"] = os.pathsep.join([str(sdk / "bin/64bit"), str(sdk / "bin"),
                                      *(str(p / "bin") for p in deps), env["PATH"]])
    else:
        env["DYLD_FRAMEWORK_PATH"] = os.pathsep.join(str(p / sub)
            for p in [sdk, *deps] for sub in ["Frameworks", "lib"])
        env["DYLD_LIBRARY_PATH"] = os.pathsep.join(str(p / "lib") for p in [sdk, *deps])
    report = work / "ctest.xml"
    run("ctest", "--test-dir", build, "-C", "Release", "--output-on-failure",
        "--no-tests=error", "--timeout", "90", "--output-junit", report, env=env)
    test_count = verify_test_report(report, args.minimum_tests)
    stage = work / "stage"
    prefix = stage / MODULE if target == "windows-x64" else stage
    run("cmake", "--install", build, "--config", "Release", "--prefix", prefix)
    binary = verify_stage(stage, target, version)
    if target.startswith("macos"):
        # Ad-hoc signature for development only; not Developer ID notarization.
        run("codesign", "--force", "--sign", "-", stage / f"{MODULE}.plugin")
        run("codesign", "--verify", "--strict", stage / f"{MODULE}.plugin")
    archive = work / f"{MODULE}-{target}-obs{version}-test.zip"
    # Preserve Unix permission bits (including executable) for macOS.
    if target.startswith("macos"):
        run("ditto", "-c", "-k", "--keepParent", stage / f"{MODULE}.plugin", archive)
    else:
        shutil.make_archive(str(archive.with_suffix("")), "zip", stage)
    with zipfile.ZipFile(archive) as z:
        member = binary.relative_to(stage).as_posix()
        if z.read(member) != binary.read_bytes() or z.testzip() is not None:
            raise RuntimeError("Archive integrity failure")
    checksum = f"{digest(archive)}  {archive.name}\n"
    (work / "SHA256SUMS").write_text(checksum)
    print(f"{target}: {test_count} tests; native module/notice archive validated")
    if os.getenv("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write(f"### {target}: native build and test archive (verified locally)\n\n")
            f.write(f"OBS {version}; {test_count} tests passed.\n\n```\n{checksum}```\n")


if __name__ == "__main__":
    main()
