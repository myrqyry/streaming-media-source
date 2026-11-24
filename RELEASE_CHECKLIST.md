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
- Provide an installable Linux package with the supported OBS/Qt ABI and
  reproducible build instructions. Current development target is OBS 32.x
  native Linux, not Windows/macOS/Flatpak. Keep the source ID and install
  module name stable across repository renames.
- Run deterministic CTest plus real playback, queue, injected decoder
  recovery, source resizing, WebSocket and source teardown verification.
  Validate production GPU decoding separately from Xvfb/software decoding.
- Add a current dock screenshot from a disposable scene, an issue template
  requesting OBS/plugin versions and redacted logs, and clear dependency
  installation/troubleshooting instructions.
- Describe direct public-media playback accurately; do not promise
  universally ad-free video, access to restricted media, or supported
  providers/platforms that have not been verified. No account credentials
  are needed for supported public VOD playback.

A private repository can contain development history that would be
inappropriate for a public issue or release. The repository owner should
approve publication after reviewing these points.
