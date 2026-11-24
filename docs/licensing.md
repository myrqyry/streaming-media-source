# Licensing and distribution

## Project license

The original Streaming Media Source project code and accompanying original
documentation are licensed under the GNU General Public License, version 3
or (at your option) any later version (SPDX: GPL-3.0-or-later). See the accompanying LICENSE file and
[GNU GPLv3 text](https://www.gnu.org/licenses/gpl-3.0.html). Third-party works retain their own copyright and
license terms; the project license does not relicense those works.

This choice allows use, modification and redistribution, including commercial
redistribution, subject to the applicable GPL terms. It does not transfer
copyright ownership.

## Dependency boundary

| Component | Applicable license / status | Distribution boundary |
| --- | --- | --- |
| OBS libobs and frontend API | OBS source terms; checked libobs headers specify GPL-2.0-or-later | Linked to the user's native OBS installation; no OBS binaries copied into our module-only package |
| obs-websocket API header | GPL-2.0-or-later in the pinned header | Used at build time; preserve its upstream notice |
| Qt 6 Core, Widgets, Network | Applicable open-source LGPL/GPL terms; GPLv3 path available | Uses compatible OBS Qt libraries; no Qt binaries copied into our module-only package |
| yt-dlp source | Unlicense | Separate upstream process |
| Standalone PyInstaller yt-dlp | GPLv3+ plus component-specific notices | Separate upstream process; no redistribution in public module-only package |
| Deno | MIT plus transitive dependency licenses | Separate upstream process; no redistribution in public module-only package |

References:

- [OBS source and license](https://github.com/obsproject/obs-studio/tree/32.2.0)
- [Pinned WebSocket API header](https://github.com/obsproject/obs-websocket/blob/1ef34bf48110c2a18184e50e41cd0b1a855e2147/lib/obs-websocket-api.h)
- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- [yt-dlp license and standalone binary terms](https://github.com/yt-dlp/yt-dlp/tree/2026.11.23#license)
- [Deno MIT license](https://github.com/denoland/deno/blob/v2.9.8/LICENSE.md)
- [GNU GPLv3](https://www.gnu.org/licenses/gpl-3.0.html), including sections 4–6 on source and object-code distribution.

See [third-party runtime notices](https://github.com/myrqyry/streaming-media-source/tree/master/third-party) for pinned
runtime versions and notices. Confirm the actual SDK and dependency
licenses for every new platform and build configuration.

## Preferred public release model

Publish the plugin module, project license and notices together with
equivalent no-extra-charge access to the exact corresponding source and
build/install scripts for that artifact. Record its source revision,
OBS SDK version, Qt version, toolchain, pinned dependencies and SHA-256.
Provide any required corresponding source for the covered combined work
under GPLv3 section 6; do not assume every dynamically linked library is
automatically a System Library. Do not rely solely on a moving branch or
an upstream homepage. Keep the source offer usable for as long as required.

For the initial module-only distribution, the user explicitly downloads
official upstream executables using the dock links and places them in a
private writable runtime directory. See the
[runtime setup guide](https://github.com/myrqyry/streaming-media-source/blob/master/docs/runtime-setup.md).
The user verifies the downloaded files against published checksums. No
system PATH changes are required. The plugin does not download or
redistribute these files on the user's behalf in this mode.

Automatic first-run provisioning remains optional future work. The
existing updater remains specific to Linux x86_64 bundled installations.
Windows and macOS asset instructions are prepared; native builds remain
unqualified. The existing
bundled archive is a local development/testing artifact, not a cleared
public distribution. If we later host bundled third-party executables,
first satisfy their exact corresponding-source and dependency-notice
obligations, including those of the PyInstaller build and Deno dependencies.

## Source provenance

The project contains its own native C++/Qt implementation, tests and
build scripts, together with the separately identified third-party
license texts. Linux SDK preparation was adapted from the project's
Framerot-family work, as recorded in the script and CI guide. Confirm
rights to any externally copied material and retain any applicable
upstream copyright or attribution notices before releasing. Do not
assume a public repository alone establishes a license or clears every
redistribution obligation.

This is a technical release policy, not a formal legal opinion.
