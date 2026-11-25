# Build and install

[Back to overview](../README.md)

## Requirements

- OBS Studio 32.x, native Linux installation (development/verification target:
  OBS 32.2.0, libobs 32.0.1 headers, Qt 6.4.2). OBS 33.0.3 is a separate
  SDK target; see the [OBS version compatibility guide](ci.md#obs-32-and-33-compatibility).
- The bundled Linux x86_64 package includes the standalone yt-dlp
  extractor and Deno JavaScript runtime. No separate Python, Node,
  yt-dlp, curl executable or PATH configuration is required.
- Source-only builds without the bundle automatically discover a complete
  yt-dlp + Deno or Node installation on PATH, in ~/.local/bin or in the
  existing NVM layout. The same Qt HTTPS rendition validation is used.
- CMake and Qt6 Core/Widgets/Network and libobs/frontend API development
  headers for building.

### OBS version and install layout

Build against the SDK matching your OBS major version. OBS 33 requires a newer
build environment (Ubuntu 26.04 with FFmpeg 8 or newer for Linux CI); do not
replace system libraries on an OBS 32 workstation to build it. A module
compiled against OBS 33 must not be installed in OBS 32.

| Host | OBS 32 package | OBS 33 package |
| --- | --- | --- |
| Linux | `obs-streaming-media/bin/64bit/obs-streaming-media.so` under `$XDG_CONFIG_HOME/obs-studio/plugins` (default `~/.config`) | `obs-streaming-media/obs-streaming-media.so` under `$XDG_DATA_HOME/obs-studio/plugins` (default `~/.local/share`) |
| Windows x64 | `obs-streaming-media/bin/64bit/obs-streaming-media.dll` | `obs-streaming-media/obs-streaming-media.dll` |
| macOS | `obs-streaming-media.plugin` | `obs-streaming-media.plugin` |

Windows per-plugin packages belong under `%ProgramData%/obs-studio/plugins`;
macOS uses the standard OBS plugin bundle directory. Preserve the accompanying
`data/` directory (bundle resources on macOS). Do not install both layouts
for the same plugin at once. Close OBS before replacing an installation.
Both SDK versions passed the merged baseline build matrix; actual OBS 33 playback is not yet qualified. See [CI and package verification](ci.md).

## Media engine setup

The module-only build can use user-installed official executables without
PATH configuration. In the Streaming Media dock, open the media engine
menu, download yt-dlp and Deno, open the runtime folder, put the two
executables there, and refresh engine status.

See the [runtime setup guide](runtime-setup.md) for direct downloads,
SHA-256 verification and step-by-step instructions. Linux x86_64 is the
verified playback target; Windows and macOS runtime playback is not yet qualified.

## Install a local test package

First generate the Linux x86_64 bundled archive using the [build and install](#compile-and-install)
instructions below (the example is for OBS 32). From the directory containing the archive and its checksum,
verify the archive, close OBS, and extract it into the per-user plugins directory:

```sh
sha256sum -c streaming-media-source-0.6.0-linux-x86_64-bundled.tar.gz.sha256
mkdir -p "$HOME/.config/obs-studio/plugins"
tar -xzf streaming-media-source-0.6.0-linux-x86_64-bundled.tar.gz \
  -C "$HOME/.config/obs-studio/plugins"
```

Restart OBS, add **Streaming Media**, and open **Docks →
Streaming Media**. The archive contains the plugin, extractor, Deno and
runtime notices. No separate runtime setup or system-wide installation is
required. Public binary releases are not yet available.

## Compile and install

```sh
# Maintainers: prepare verified upstream runtime executables (Python 3.11+).
python3 scripts/prepare-runtime.py --output build/bundle/data/runtime --cache build/runtime-downloads
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DSTREAMING_MEDIA_RUNTIME_DIR="$PWD/build/bundle/data/runtime"
cmake --build build -j2
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.config/obs-studio/plugins/obs-streaming-media"
```

The commands above target OBS 32 on Linux. Close OBS before installing. For an OBS 33 SDK, use a separate build
directory, select `-DSTREAMING_MEDIA_PACKAGE_LAYOUT=modern` and stage under
`"${XDG_DATA_HOME:-$HOME/.local/share}/obs-studio/plugins/obs-streaming-media"`.
The default `auto` layout follows the detected libobs version; CI explicitly
selects the corresponding layout. Verify the build against its intended
host before installing.

The bundle is installed under the module's own `data/runtime/` directory.
It is preferred over system tools. For a source-only development build,
configure with `-DSTREAMING_MEDIA_RUNTIME_DIR=` and supply an existing
yt-dlp plus supported JavaScript engine. The install is local; no packages
are installed globally. Runtime pins are intentionally explicit and
downloaded assets are checked against recorded upstream SHA-256 digests.
The dock's media engine menu offers **Check for extractor updates** and
**Use bundled extractor**. Updates are fetched only on request from the
official stable yt-dlp release, verified against the release asset SHA-256
and size, and version-checked before activation. The updated executable
is stored in the OBS plugin configuration directory; the original bundle
remains untouched. The next resolution uses the new engine without an OBS
restart. Use the bundled extractor to roll back. Deno remains pinned.
See [third-party runtime notices](../third-party/README.md) and the release
checklist before distributing binaries.

To generate a local package after the build, use:

```sh
cpack --config build/CPackConfig.cmake -B build/packages
```

CPack produces a compressed archive and SHA-256 file. The system-runtime
build produces a separately named archive without the bundled executables.
Do not upload bundled archives until the redistribution review is complete.

Restart OBS when convenient. In source properties, paste a supported public media URL and
select maximum resolution, hardware decoding, playback speed and network
buffer. The source has OBS's normal media controls. Restart resolves a
fresh media URL; expired URLs and extraction errors can require a fresh resolution.
Quality is a cap, not a guarantee: when separate renditions are unavailable,
the resolver may select a lower-quality combined video/audio stream.
