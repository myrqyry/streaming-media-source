# Public release readiness

The GitHub repository is currently private. Changing visibility, publishing
a release or rewriting history is a separate owner decision.

Before a first public release:

- Select a license compatible with the linked OBS Studio GPL code and
  document the license/attribution of included third-party material. OBS
  ships GPLv2 and many of its source headers permit later versions.
  Confirm the applicable terms before distributing the plugin; no license
  has been selected for this repository yet.
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
  Current target is native OBS 32.x, not Windows/macOS/Flatpak. Keep the
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
  The pinned OBS32 local CI driver passed; hosted CI is currently blocked
  by an Actions budget. No hosted build/test steps ran.
- Describe direct public-media playback accurately; do not promise
  universally ad-free video, access to restricted media, or supported
  providers/platforms that have not been verified. No account credentials
  are needed for supported public VOD playback.

A private repository can contain development history that would be
inappropriate for a public issue or release. The repository owner should
approve publication after reviewing these points.
