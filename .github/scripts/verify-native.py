"""Build and verify an OBS32 module-only package on a native Windows/macOS host."""
import argparse
import hashlib
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

VERSION = "32.2.0"
SOURCE_SHA = "c333e4a7d5c4a94c7bb4833f046368e0ac5e981fb3964ca4981b23be1dfbda4a"
WS_REV = "1ef34bf48110c2a18184e50e41cd0b1a855e2147"
WS_SHA = "c29e8e38ee66c36db79cae86217ab0f270aad39f97e82090809d087c281b7948"
MODULE = "obs-streaming-media"
ROOT = Path(__file__).resolve().parents[2]


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


def source_archive(work):
    archive = work / "obs.tar.gz"
    download(f"https://github.com/obsproject/obs-studio/archive/refs/tags/{VERSION}.tar.gz",
             archive, SOURCE_SHA)
    with tarfile.open(archive) as tar:
        # The source archive contains symlinks to unused upstream shell
        # scripts. Windows need not have symlink privileges to extract them.
        members = [m for m in tar.getmembers() if not m.issym() and not m.islnk()]
        tar.extractall(work, members=members, filter="data")
    return work / f"obs-studio-{VERSION}"


def patch_once(path, old, new):
    """Apply only an exact, reviewed change to the checksum-pinned SDK."""
    data = path.read_bytes()
    old, new = old.encode(), new.encode()
    if data.count(old) != 1:
        raise RuntimeError(f"Unexpected OBS {VERSION} source: {path}; expected exactly one occurrence of {old!r}")
    path.write_bytes(data.replace(old, new, 1))


def patch_obs_sdk(obs, target):
    if target == "windows-x64":
        # OBS 32.2.0's boolean list getter reads the double union member,
        # although add_item/insert_item store the boolean member.
        patch_once(obs / "libobs/obs-properties.c",
                   "data->items.array[idx].d : false;",
                   "data->items.array[idx].b : false;")
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


def dependency_dirs(obs, target):
    # OBS also builds/downloads x86 helper dependencies on Windows.
    # Only the native x64 (or universal macOS) pair belongs on the
    # plugin's CMake prefix and runtime library search paths.
    arch = "x64" if target == "windows-x64" else "universal"
    base = obs / ".deps"
    deps = [base / f"obs-deps-2026-07-15-{arch}",
            base / f"obs-deps-qt6-2026-07-15-{arch}"]
    for path, label in zip(deps, ("OBS", "Qt")):
        if not path.is_dir():
            raise RuntimeError(f"Missing pinned {label} dependency directory: {path}")
    return deps


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


def verify_stage(stage, target):
    if target == "windows-x64":
        binary = stage / MODULE / "bin/64bit" / f"{MODULE}.dll"
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
        arch = target.removeprefix("macos-")
        run("lipo", binary, "-verify_arch", arch)
        run("otool", "-L", binary)
    for path, source in [(notices / "LICENSE", ROOT / "LICENSE"),
                         (notices / "licensing.md", ROOT / "docs/licensing.md")]:
        if path.read_bytes() != source.read_bytes():
            raise RuntimeError(f"Missing or altered notice: {path}")
    return binary


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("work", type=Path)
    args = p.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    target, generator = native_config()
    obs = source_archive(work)
    patch_obs_sdk(obs, target)
    sdk = work / "sdk"
    obs_build = work / "obs-build"
    # OBS's source archive pins the matching obs-deps and Qt hashes in
    # CMakePresets.json. Let the upstream dependency bootstrap enforce them.
    run("cmake", "-S", obs, "-B", obs_build, *generator,
        "-DOBS_CMAKE_VERSION:STRING=3.0.0", f"-DOBS_VERSION_OVERRIDE:STRING={VERSION}",
        "-DENABLE_PLUGINS=OFF", "-DENABLE_FRONTEND=OFF",
        "-DENABLE_BROWSER=OFF", "-DENABLE_SCRIPTING=OFF")
    run("cmake", "--build", obs_build, "--config", "Release",
        "--target", "obs-frontend-api", "--parallel", "4")
    run("cmake", "--install", obs_build, "--config", "Release",
        "--component", "Development", "--prefix", sdk)
    deps = dependency_dirs(obs, target)
    header = work / "obs-websocket-api.h"
    download(f"https://raw.githubusercontent.com/obsproject/obs-websocket/{WS_REV}/lib/obs-websocket-api.h",
             header, WS_SHA)
    build = work / "build"
    prefixes = ";".join(p.as_posix() for p in [sdk, *deps])
    run("cmake", "-S", ROOT, "-B", build, *generator,
        f"-DCMAKE_PREFIX_PATH={prefixes}",
        f"-Dlibobs_DIR={config_dir(sdk, 'libobs').as_posix()}",
        f"-Dobs-frontend-api_DIR={config_dir(sdk, 'obs-frontend-api').as_posix()}",
        f"-DOBS_WEBSOCKET_INCLUDE_DIR={work.as_posix()}", "-DSTREAMING_MEDIA_RUNTIME_DIR=")
    run("cmake", "--build", build, "--config", "Release", "--parallel", "4")
    env = os.environ.copy()
    qt = deps[1]
    env["QT_PLUGIN_PATH"] = str(qt / "plugins")
    env["QT_QPA_PLATFORM"] = "offscreen"
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
    cases = ET.parse(report).findall(".//testcase")
    if len(cases) != 9 or any(c.find("skipped") is not None or c.find("failure") is not None
                             or c.find("error") is not None or c.get("status") in ("notrun", "disabled")
                             for c in cases):
        raise RuntimeError("Native regression suite incomplete or failed")
    stage = work / "stage"
    prefix = stage / MODULE if target == "windows-x64" else stage
    run("cmake", "--install", build, "--config", "Release", "--prefix", prefix)
    binary = verify_stage(stage, target)
    if target.startswith("macos"):
        # Ad-hoc signature for development only; not Developer ID notarization.
        run("codesign", "--force", "--sign", "-", stage / f"{MODULE}.plugin")
        run("codesign", "--verify", "--strict", stage / f"{MODULE}.plugin")
    archive = work / f"{MODULE}-{target}-obs32-test.zip"
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
    print(f"{target}: {len(cases)} tests; native module/notice archive validated")
    if os.getenv("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write(f"### {target}: native build and test archive (not uploaded)\n\n")
            f.write(f"OBS {VERSION}; {len(cases)} tests passed.\n\n```\n{checksum}```\n")


if __name__ == "__main__":
    main()
