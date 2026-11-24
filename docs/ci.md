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
2. Download OBS 32.2.0 source and verify its pinned SHA-256 before
   building and installing the SDK into runner-local storage.
3. Configure a clean Release build with explicit SDK CMake package paths.
4. Require the native CTest suite to register at least the baseline number
   of tests and pass under Xvfb/software rendering without skipped tests.
5. Stage the module via CMake install, enforce the exact native module and project license notice
   payload, reject embedded runtime search paths, and check shared libraries.
6. Create a temporary archive, verify its SHA-256, extract it separately,
   and compare the extracted module to the staged module.

The workflow runs on pull requests, pushes to main/master, and manual
dispatch. It has read-only repository permissions and no automatic
release or binary upload. The checksum is recorded in the run summary.
A passing job establishes the tested Linux SDK/build/package path, not
interactive OBS behavior, GPU-driver coverage, or cross-platform support.
No user OBS profile is modified by the verifier.

## Reproduce on a developer machine

Run from the project root with build dependencies installed. For the
exact CI SDK, use an empty temporary directory and run:

    bash .github/scripts/prepare-obs32-sdk.sh /tmp/plugin-obs32-sdk

Then run the project-specific verifier (see the workflow for the module
name and baseline test count) against /tmp/plugin-obs32-sdk/sdk and a
new empty work directory. You may instead pass a matching local SDK prefix
such as /usr/local; that result qualifies the installed local SDK only.
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
It uses the same checksum-pinned OBS 32.2.0 source archive and WebSocket
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

The new jobs are qualification in progress, not a support announcement.
Neither CI test archives nor release binaries are uploaded. Actual OBS
load, playback, audio, playlist imports, and clean teardown on the target
platforms remain necessary before claiming production support.
