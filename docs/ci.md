# CI and package verification

The initial Linux CI pilot adapts the pinned OBS SDK build from Framerot
(commit 16d1ece8), the strict test gate and version-specific build approach
from Timeline, and the package/content integrity checks from both.
The two pilot repositories carry self-contained copies so neither needs
access to another private repository at build time. Promote a shared,
versioned workflow only after the pilots qualify.

## Verification stages

1. Check common relative Markdown links and HTML image sources in README.
   These checks do not validate remote URLs or heading anchors.
2. Download the selected pinned OBS source and verify its pinned SHA-256 before
   building and installing the SDK into runner-local storage.
3. Configure a clean Release build with explicit SDK CMake package paths.
4. Require the native CTest suite to register at least the baseline number
   of tests and pass under Xvfb/software rendering without skipped tests.
5. Stage the module via CMake install, enforce the exact native module and project license notice
   payload, reject embedded runtime search paths, and check shared libraries.
6. Create a temporary archive, verify its SHA-256, extract it separately,
   and compare the extracted module to the staged module.

The workflow runs on pull requests, pushes to main/master and the qualification branches, and manual
dispatch. It has read-only repository permissions and no automatic
release. Verified module-only test archives and their SHA-256 checksums are uploaded with 14-day retention after all build/test/package gates pass. The checksum is recorded in the run summary.
A passing job establishes the tested Linux SDK/build/package path, not
interactive OBS behavior, GPU-driver coverage, or cross-platform support.
No user OBS profile is modified by the verifier.

## Reproduce on a developer machine

Run from the project root with build dependencies installed. For the
exact CI SDK, use an empty temporary directory and run:

    bash .github/scripts/prepare-obs32-sdk.sh /tmp/plugin-obs32-sdk

Then run the project-specific verifier (see the workflow for the module
name and baseline test count) against /tmp/plugin-obs32-sdk/sdk and a
new empty work directory. The verifier requires an SDK prepared with the version marker
`OBS_SDK_VERSION`; do not relabel an unrelated installed SDK.
Keep the live OBS application and profile separate from build staging.

## Local qualification — 2026-11-24

The pinned OBS 32.2.0 SDK was downloaded, checksum-verified and built
from source. A clean Release build of the committed source plus this
CI change passed all 9 CTest cases, the module-only package allowlist,
shared-library checks, SHA-256 verification and extraction comparison.
The first [GitHub-hosted run](https://github.com/myrqyry/streaming-media-source/actions/runs/45404314210)
was marked failed before any steps ran because of the Actions budget.
After the repository became public, [attempt 2 of the later run](https://github.com/myrqyry/streaming-media-source/actions/runs/45412791743)
completed successfully. This qualifies the existing Linux SDK/build/test
workflow, not Windows, macOS or the live playback paths.

The temporary archives are intentionally not release packages. Public
redistribution requires the owner to establish licensing, provenance and
platform qualification first. Never publish local logs, scenes, models,
private development notes, or third-party executables by default.

## Windows and macOS qualification

The native qualification matrix adds Windows x64 (Visual Studio 2026),
macOS Apple Silicon and macOS Intel (Xcode) to the existing Linux job.
It uses the selected checksum-pinned OBS source archive and WebSocket
API header; OBS's own dependency bootstrap verifies the matching Qt and
obs-deps archives specified in its CMakePresets.json.

Run on a matching native host with Python 3.12+, CMake and the appropriate
compiler and SDK installed:

    python .github/scripts/verify-native.py <new-empty-work-directory>

The job builds the OBS development SDK, builds the plugin and runs all
nine deterministic regression executables. The Linux-only updater's
filesystem activation tests are only applicable on Linux; its release
metadata validation is tested on all platforms. The live YouTube/X11
harness remains Linux-only.

Windows stages a per-plugin folder with bin/64bit/obs-streaming-media.dll
and data/licenses. macOS stages obs-streaming-media.plugin with the native
Mach-O binary, bundle metadata and license resources. The jobs validate
architecture, installed contents, archive integrity and SHA-256. macOS
uses an ad-hoc signature for test purposes, not Developer ID notarization.
No yt-dlp or Deno executables, runtime dependency DLLs, or third-party
frameworks are copied into these packages.

The OBS 32.2.0 baseline passed all four hosted jobs in [run 45528453220](https://github.com/myrqyry/streaming-media-source/actions/runs/45528453220). The later merged video/audio baseline passed all eight OBS 32/33 matrix jobs in [run 45542976014](https://github.com/myrqyry/streaming-media-source/actions/runs/45542976014). TikTok-specific changes remain outside that hosted qualification.
CI test archives are eligible for artifact upload; no release binaries are published. Actual OBS
load, playback, audio, playlist imports, and clean teardown on the target
platforms remain necessary before claiming production support.

### OBS 32.2.0 SDK compatibility corrections

The first native qualification run (45511857181) passed Linux but exposed
two upstream SDK defects. MSVC rejects the boolean list getter's read of
the double union member (C4244/C2220); the getter must read the boolean
member used by the corresponding insertion functions. Both Mac runners
built libobs but failed to resolve the generated framework-relative
imported target. The native verifier applies one exact, checked source
replacement for each affected platform after verifying the upstream
archive checksum. macOS CMake package exports move to lib/cmake while
frameworks and headers stay in their normal locations. The patches fail
closed if the original source text changes. They affect only the
temporary SDK build, not the user's OBS installation or our plugin.

The second qualification pass (PR run 45512874672, commit 72af0c8)
successfully built the Windows SDK and compiled and ran all nine tests
on both Mac architectures. Windows stopped at dependency-directory
selection: upstream also provisions x86 helper dependencies. The verifier
now selects the exact pinned x64 pair and rejects a missing required
directory. macOS installation succeeded but the lipo invocation had
the input file after -verify_arch; the input now precedes the operation.
The deployment target is macOS 13.0 to match the linked Qt framework
minimum reported by the linker. Regression tests reproduce both
verifier defects. Native package qualification still requires a green
rerun; live playback remains a separate gate.

### Windows headless Qt platform

PR run [45514383898](https://github.com/myrqyry/streaming-media-source/actions/runs/45514383898)
at 1f1f7c8 passed Linux and both Mac native build/test/archive jobs.
Windows compiled successfully and passed eight of nine tests; the
dock-theme-test timed out with no diagnostic output. Inspection of the
central directory of the exact pinned Windows Qt dependency asset
(windows-deps-qt6-2026-07-15-x64.zip) found qminimal.dll, qwindows.dll
and qdirect2d.dll, but no qoffscreen.dll. The test had forced offscreen
on every platform. Windows now uses minimal and the verifier checks its
DLL exists before launching CTest; Linux and macOS retain offscreen.
The test emits startup/platform/teardown diagnostics to stderr.
The narrow-layout test assigns the dock width explicitly, verifying
a preview no wider than 80 pixels at a 240-pixel dock width rather than
depending on top-level window manager sizing or default fonts.
The same assertions pass locally with both Qt backends. All nine Linux
tests and four Python verifier tests passed. Hosted Windows qualification
requires a successful rerun. The pinned Mac universal dependency choice
is tested for both architectures; OBS/Qt dependencies are explicitly
unpacked by role.

## OBS 32 and 33 compatibility

The shared pin file [obs-sdk-pins.json](../.github/scripts/obs-sdk-pins.json)
records source SHA-256, obs-websocket submodule revision and header hash,
and the dependency release for both versions.

| Version | Linux runner | Native dependency release | Windows / Linux layout |
| --- | --- | --- | --- |
| 32.2.0 | Ubuntu 24.04 | 2026-07-15 | Legacy `bin/64bit/` |
| 33.0.3 | Ubuntu 26.04 | 2026-10-23 | Module at plugin directory root |

Both versions use the Visual Studio 2026 x64 runner and native Xcode
runners for Apple Silicon and Intel. Mac packages remain plugin bundles.
OBS 33 Linux requires FFmpeg 8 or newer. Use a matching isolated build
environment; the developer's Linux Mint Qt 6.4 / FFmpeg 6 libraries are
not an OBS 33 SDK.

Linux reproduction, with the matching runner's dependencies installed:

```sh
bash .github/scripts/prepare-obs32-sdk.sh /tmp/plugin-obs33-sdk 33.0.3
bash .github/scripts/verify-linux.sh /tmp/plugin-obs33-sdk/sdk /tmp/plugin-obs33-ci obs-streaming-media 9
```

The preparation script retains its historical filename and defaults to
32.2.0 when no version is supplied. Native Windows/macOS reproduction:

```sh
python .github/scripts/verify-native.py /path/to/new-work-directory --obs-version 33.0.3
```

The OBS 32 Windows boolean-property correction is already present
upstream in 33.0.3. The verifier checks that corrected source instead
of applying the old patch. The exact macOS framework export correction
remains applicable. Both were checked against the actual OBS 33 archive.

CMake supports `STREAMING_MEDIA_PACKAGE_LAYOUT=auto|legacy|modern`.
Automatic selection uses the detected libobs version; CI selects the
expected layout explicitly and checks staged paths. The Linux OBS 33
per-user install root is `$XDG_DATA_HOME/obs-studio/plugins`, defaulting
to `~/.local/share/obs-studio/plugins`. Windows uses
`%ProgramData%/obs-studio/plugins`. Preserve the stable module/source
identity and never keep duplicate legacy and modern installations.

The OBS 33 archive/header hashes and exact upstream patch targets were
verified locally; the seven Python verifier tests and all nine C++
regressions passed with the pinned OBS 32 SDK. Both legacy and modern
package layouts were staged with identical module bytes using that SDK.
That validates packaging, not OBS 33 binary compatibility. The subsequent merged video/audio baseline passed actual native builds, deterministic tests and packaging across the complete OBS 32/33 matrix ([run 45542976014](https://github.com/myrqyry/streaming-media-source/actions/runs/45542976014)). Actual OBS 33 playback and the newer TikTok matrix remain unqualified.
The full matrix covers eight jobs. The reusable workflow uploads exactly one verified module-only test archive plus SHA256SUMS per job; hosted artifact upload on this new branch remains to be qualified. No release is created.

### Public artifact names

Downloadable test artifacts and their enclosed archives use
`streaming-media-source-<platform>-obs<version>-test` (archive suffix
.tar.gz or .zip). The installed module and its existing directories retain
`obs-streaming-media` to preserve compatible upgrades. The original shared
CI run 45558327793 passed all eight jobs and uploaded eight artifacts;
the public-name workflow revision passed the eight-job matrix and uploaded
eight correctly named archives in [run 45566510651](https://github.com/myrqyry/streaming-media-source/actions/runs/45566510651).
The reconciled resume-controls baseline raises the required CTest minimum to 10;
its new hosted matrix remains to be verified.
