#!/usr/bin/env bash
# Build against an explicit SDK; verify native tests and staged package.
# Never install into a user's OBS profile. The caller may upload the verified module-only test package.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
sdk="$(realpath "$1")"
work="$(realpath -m "$2")"
module="$3"
minimum_tests="$4"
mkdir -p "$work"
build="$work/build"
stage="$work/stage"
test ! -e "$stage"
test -s "$sdk/lib/cmake/libobs/libobsConfig.cmake"
test -s "$sdk/lib/cmake/obs-frontend-api/obs-frontend-apiConfig.cmake"
version="$(cat "$sdk/OBS_SDK_VERSION")"
pins="$root/.github/scripts/obs-sdk-pins.json"
pin_values="$(python3 - "$pins" "$version" <<'PIN'
import json, sys
with open(sys.argv[1]) as f:
    pin = json.load(f)[sys.argv[2]]
print(pin["websocket_revision"], pin["websocket_sha256"])
PIN
)"
read -r rev digest <<< "$pin_values"
case "$version" in
  32.2.0) layout=legacy; module_dir=bin/64bit/; archive_version=obs32.2.0 ;;
  33.0.3) layout=modern; module_dir=; archive_version=obs33.0.3 ;;
  *) echo "Unsupported OBS SDK: $version" >&2; exit 1 ;;
esac
args=()
if [[ "$module" == obs-streaming-media ]]; then
  # Each pinned OBS release records its own obs-websocket gitlink. Source archives omit
  # submodule contents; fetch and verify the exact public API header.
  mkdir -p "$work/headers"
  header="$work/headers/obs-websocket-api.h"
  curl -fsSL --retry 3 "https://raw.githubusercontent.com/obsproject/obs-websocket/$rev/lib/obs-websocket-api.h" -o "$header"
  printf '%s  %s\n' "$digest" "$header" | sha256sum -c -
  args+=("-DOBS_WEBSOCKET_INCLUDE_DIR=$work/headers" "-DSTREAMING_MEDIA_RUNTIME_DIR=" "-DSTREAMING_MEDIA_PACKAGE_LAYOUT=$layout")
elif [[ "$module" != obs-ui-augment || "$version" != 32.2.0 ]]; then
  echo "Unsupported pilot module: $module" >&2
  exit 2
fi
export PYTHONDONTWRITEBYTECODE=1
python3 "$root/.github/scripts/test-readme-links.py"
python3 "$root/.github/scripts/check_readme_links.py" "$root/README.md"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON "-Dlibobs_DIR=$sdk/lib/cmake/libobs" \
  "-Dobs-frontend-api_DIR=$sdk/lib/cmake/obs-frontend-api" "${args[@]}"
cmake --build "$build" --parallel 4
ctest --test-dir "$build" --show-only=json-v1 > "$work/tests.json"
python3 - "$work/tests.json" "$minimum_tests" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    count = len(json.load(f)["tests"])
assert count >= int(sys.argv[2]), f"Only {count} tests registered"
print(f"Registered {count} tests")
PY
export LD_LIBRARY_PATH="$sdk/lib:$sdk/lib/obs-plugins${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LIBGL_ALWAYS_SOFTWARE=1
xvfb-run -a ctest --test-dir "$build" --output-on-failure \
  --no-tests=error --timeout 60 --output-junit "$work/ctest.xml"
python3 - "$work/ctest.xml" "$minimum_tests" <<'PY'
import sys, xml.etree.ElementTree as ET
cases = ET.parse(sys.argv[1]).findall(".//testcase")
assert len(cases) >= int(sys.argv[2]), "Incomplete CTest report"
assert all(c.find("skipped") is None and c.find("failure") is None and
           c.find("error") is None and c.get("status") not in ("notrun", "disabled")
           for c in cases), "A registered test was skipped or failed"
print(f"Verified {len(cases)} test results")
PY
cmake --install "$build" --prefix "$stage/$module"
binary="$stage/$module/${module_dir}$module.so"
test -s "$binary"
# Exact payload allowlist: native module and required project notices only; no configuration, source, bundled runtimes,
# or build outputs accidentally enter this preliminary module-only package.
expected="$module/${module_dir}$module.so"
if [[ "$module" == obs-streaming-media ]]; then
  expected=$(printf '%s\n' "$expected" "$module/data/licenses/LICENSE" "$module/data/licenses/licensing.md" | LC_ALL=C sort)
  cmp "$root/LICENSE" "$stage/$module/data/licenses/LICENSE"
  cmp "$root/docs/licensing.md" "$stage/$module/data/licenses/licensing.md"
fi
test "$(find "$stage" -mindepth 1 ! -type d -printf '%P\n' | LC_ALL=C sort)" = "$expected"
if readelf -d "$binary" | grep -E '(RPATH|RUNPATH)' ; then
  echo "Unexpected embedded runtime search path" >&2
  exit 1
fi
ldd "$binary" > "$work/dependencies.txt"
if grep -F "not found" "$work/dependencies.txt"; then exit 1; fi
tar -C "$stage" -czf "$work/$module-linux-x86_64-${archive_version}-test.tar.gz" "$module"
tar -tzf "$work/$module-linux-x86_64-${archive_version}-test.tar.gz" > "$work/package-members.txt"
grep -Fx "$module/${module_dir}$module.so" "$work/package-members.txt"
(cd "$work" && sha256sum "$module-linux-x86_64-${archive_version}-test.tar.gz" > SHA256SUMS && sha256sum -c SHA256SUMS)
mkdir "$work/extracted"
tar -xzf "$work/$module-linux-x86_64-${archive_version}-test.tar.gz" -C "$work/extracted"
cmp "$binary" "$work/extracted/$module/${module_dir}$module.so"
echo "Native tests and local package validation passed; not a release."
