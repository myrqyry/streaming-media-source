# Changelog

This log describes development checkpoints. No public release has been
published.

## 0.6.0 — Playlist imports

- Import supported public YouTube playlists through the existing dock's
  Play and Queue controls.
- Append without disturbing playback, or play the first available video
  and insert the remaining entries before the existing queue.
- Choose between a single video and the entire playlist for mixed links.
- Cancellable, bounded metadata enumeration with actual import receipts.
- Reuse playlist titles, channel names and known durations in the queue.
- Share the bundled/system runtime selection with ordinary playback.

Verified: nine native CTest cases; real bundled playlist import, cancellation,
queue capacity, uninterrupted playback, metadata, first-video playback and
automatic advancement setting; normal playback regression. The import smoke
passed 24,178 concurrent dimension queries and the normal regression passed
50,741. Both used an empty PATH and fresh HOME.

Limits: at most 101 entries enumerated per import and 100 upcoming queue
entries; unknown availability can fail at playback time. Imported metadata
is session-only. Mixed-link choice and source-change cancellation were not
independently exercised in the live smoke.

## 0.5.1 — Runtime updates and local packages

- Explicit stable yt-dlp updates with asset size and SHA-256 checks,
  executable version validation, atomic activation and bundled rollback.
- Local Linux x86_64 install archive with checksum, upstream notices,
  installation documentation and diagnostic guidance.

## 0.5.0 — Self-contained runtime

- Pinned standalone yt-dlp and Deno bundle, with system-tool fallback.
- Native Qt HTTPS rendition validation replaces external curl.
- Verified playback and recovery without external tools on PATH.

## 0.4.1 — Visual playlists

- Thumbnail rows, title/channel metadata, native theme rendering and
  drag-to-reorder.

## 0.4.0 — Playback recovery

- Bounded fresh resolution on interruption, VOD position restoration,
  cancellation and queue preservation.

## 0.3.2 — Playback lock safety

- Independent audio handoff and atomic dimension snapshots remove the
  identified source-dimension/audio lock inversion.
- Decoder update gating and EOF handling improvements.

## 0.3.1 / 0.3.0 — Dock and request integration

- Native theme-aware dock controls and responsive thumbnail preview.
- Authenticated OBS WebSocket vendor requests for opted-in sources.

## 0.2.0 — Playback dock

- Transport, per-source upcoming queue and optional natural-end advancement.
