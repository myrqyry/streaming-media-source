#!/usr/bin/env bash
# Build against an explicit SDK; verify native tests and staged package.
# Never install into a user's OBS profile or upload a distributable.
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
args=()
if [[ "$module" == obs-streaming-media ]]; then
  # OBS 32.2.0 records this obs-websocket gitlink. Source archives omit
  # submodule contents; fetch and verify the exact public API header.
  rev=1ef34bf48110c2a18184e50e41cd0b1a855e2147
  digest=c29e8e38ee66c36db79cae86217ab0f270aad39f97e82090809d087c281b7948
  mkdir -p "$work/headers"
  header="$work/headers/obs-websocket-api.h"
  curl -fsSL --retry 3 "https://raw.githubusercontent.com/obsproject/obs-websocket/$rev/lib/obs-websocket-api.h" -o "$header"
  printf '%s  %s\n' "$digest" "$header" | sha256sum -c -
  args+=("-DOBS_WEBSOCKET_INCLUDE_DIR=$work/headers" "-DSTREAMING_MEDIA_RUNTIME_DIR=")
elif [[ "$module" != obs-ui-augment ]]; then
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
binary="$stage/$module/bin/64bit/$module.so"
test -s "$binary"
# Exact payload allowlist: no configuration, source, bundled runtimes,
# or build outputs accidentally enter this preliminary module-only package.
test "$(find "$stage" -mindepth 1 ! -type d -printf '%P\n')" = "$module/bin/64bit/$module.so"
if readelf -d "$binary" | grep -E '(RPATH|RUNPATH)' ; then
  echo "Unexpected embedded runtime search path" >&2
  exit 1
fi
ldd "$binary" > "$work/dependencies.txt"
if grep -F "not found" "$work/dependencies.txt"; then exit 1; fi
tar -C "$stage" -czf "$work/$module-linux-x86_64-obs32-test.tar.gz" "$module"
tar -tzf "$work/$module-linux-x86_64-obs32-test.tar.gz" > "$work/package-members.txt"
grep -Fx "$module/bin/64bit/$module.so" "$work/package-members.txt"
(cd "$work" && sha256sum "$module-linux-x86_64-obs32-test.tar.gz" > SHA256SUMS && sha256sum -c SHA256SUMS)
mkdir "$work/extracted"
tar -xzf "$work/$module-linux-x86_64-obs32-test.tar.gz" -C "$work/extracted"
cmp "$binary" "$work/extracted/$module/bin/64bit/$module.so"
echo "Native tests and local package validation passed; not a release."
