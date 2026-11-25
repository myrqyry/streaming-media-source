# Public release readiness

The repository is now public. Publishing release binaries or rewriting
history remains a separate owner decision.

Before a first public release:

- Project license published: GPL-3.0-or-later, using the checked
  OBS and obs-websocket GPLv2-or-later API headers and compatible Qt
  open-source licensing. See [licensing and distribution](docs/licensing.md).
  Verify source provenance before release.
  Provide corresponding source, exact dependency versions, build scripts
  and license notices with any public binary distribution.
- Review all reachable Git history, not just HEAD, for private URLs,
  credentials, local usernames, machine identifiers, incident logs and
  project-specific context. Prior scans checked common credential patterns
  but are not a guarantee. Review the historical development notes.
  Do not publish raw FFmpeg logs, debugger dumps or saved scene collections.
- The local Linux x86_64 bundle now stages pinned official yt-dlp and
  Deno executables with SHA-256 verification. No external curl is required.
  Complete upstream binary redistribution notices, corresponding source
  obligations and package ABI review before uploading public artifacts.
  The opt-in dock updater is implemented for stable yt-dlp releases;
  Deno remains pinned. Upstream license texts are included with local
  staged bundles; public distribution still needs corresponding-source
  compliance and the Deno dependency notice review.
- Local Linux x86_64 CPack TGZ package and SHA-256 checksum are implemented.
  The extracted archive has passed real playback with an empty PATH on the
  development machine; cross-distro ABI qualification is still required.
  Current qualified target is native Linux OBS 32.x; Windows/macOS
  native build/test/package qualification passed on merged CI baseline;
  current provider changes and live playback still require native verification.
  Flatpak is unqualified. Keep the
  source ID and install module name stable across repository renames.
- Run deterministic CTest plus real playback, queue, injected decoder
  recovery, source resizing, WebSocket and source teardown verification.
  Validate production GPU decoding separately from Xvfb/software decoding.
- Completed: v0.5.1 dock screenshot from a disposable OBS scene,
  issue template with version and redaction guidance, local package
  instructions and troubleshooting table. Refresh the screenshot for the
  playlist import UI before public release.
- v0.6.0 playlist import and normal playback smoke passed with the bundled
  engine; 9 native CTest cases passed. Mixed-link choice and source-change
  cancellation were not independently exercised in the live smoke.
  The pinned OBS32 local CI driver passed; hosted retry completed
  successfully after the repository became public.
- Describe direct public-media playback accurately; do not promise
  universally ad-free video, access to restricted media, or supported
  providers/platforms that have not been verified. No account credentials
  are needed for supported public VOD playback.

The repository was previously private and its existing history is now
public. Review any historical development notes for sensitive information.
No release binaries should be published until the remaining gates are met.

- OBS 33.0.3: verify all eight OBS 32/33 matrix jobs on the current provider
  branch, then exercise native module loading, real playback, queueing,
  generated thumbnails, mixer audio and clean teardown on the intended
  hosts. Validate modern install discovery and removal of duplicate legacy
  copies. Keep binaries built against OBS 33 out of OBS 32 installations.
  The SDK pins and layout verification alone do not establish compatibility.

- Streaming Audio: local OBS 32.2 mixer/transport/artwork/cross-provider EOF smokes passed; qualify audio recovery, SoundCloud collection playback/metadata, HLS artwork and remote requests in audio mode, OBS 33 and native Windows/macOS playback before declaring support.

- TikTok: owner sample passed Linux OBS 32.2 playback and normal cleanup; qualify additional posts, natural EOF from TikTok, cancellation mid-download, crash cleanup and OBS 33/Windows/macOS before broader support claims.
