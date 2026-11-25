#!/usr/bin/env bash
# Adapted from framerot-release/.github/scripts/ci-build-linux.sh at 16d1ece8.
set -euo pipefail
work="$(realpath -m "$1")"
version="${2:-32.2.0}"
pins="$(dirname "$0")/obs-sdk-pins.json"
sha="$(python3 - "$pins" "$version" <<'PIN'
import json, sys
with open(sys.argv[1]) as f:
    print(json.load(f)[sys.argv[2]]["source_sha256"])
PIN
)"
sdk="$work/sdk"
archive="$work/obs-studio-$version.tar.gz"
mkdir -p "$work"
curl -fsSL --retry 5 "https://github.com/obsproject/obs-studio/archive/refs/tags/$version.tar.gz" -o "$archive"
printf '%s  %s\n' "$sha" "$archive" | sha256sum -c -
tar -xzf "$archive" -C "$work"
cmake -S "$work/obs-studio-$version" -B "$work/obs-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$sdk" \
  -DOBS_CMAKE_VERSION:STRING=3.0.0 -DOBS_VERSION_OVERRIDE:STRING="$version" \
  -DENABLE_PLUGINS=OFF -DENABLE_FRONTEND=OFF -DENABLE_SCRIPTING=OFF -DENABLE_BROWSER=OFF
cmake --build "$work/obs-build" --target obs-frontend-api libobs-opengl --parallel 4
cmake --install "$work/obs-build" --component Development --config Release --prefix "$sdk"
cmake --install "$work/obs-build" --component Runtime --config Release --prefix "$sdk"
test -s "$sdk/lib/cmake/libobs/libobsConfig.cmake"
test -s "$sdk/lib/cmake/obs-frontend-api/obs-frontend-apiConfig.cmake"
printf '%s\n' "$version" > "$sdk/OBS_SDK_VERSION"
