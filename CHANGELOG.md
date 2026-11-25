# Changelog

This log describes development checkpoints. No public release has been
published.

## Unreleased — TikTok video

- Initial public TikTok post playback via bounded temporary MP4 download
  using bundled yt-dlp, with native OBS video/audio playback.
- Owner's sample passed real Linux OBS 32.2 playback, transport, generated
  previews, mixed-provider queue advancement and normal temporary-file cleanup.
- Broader post coverage, cancellation mid-download, crash cleanup and
  cross-platform playback remain unqualified.

## Unreleased — Streaming Audio

- Dedicated audio-only OBS source with shared transport, mixer, metadata and
  queue; public YouTube, SoundCloud and Bandcamp tracks.
- Verified real playback, seek, pause/resume and cross-provider automatic
  advancement on Linux OBS 32.2, with zero video frames.
- Add public SoundCloud set and Bandcamp album imports using the existing
  bounded queue. Bandcamp and YouTube audio-mode imports passed live tests;
  SoundCloud import queueing passed, but full metadata/playback qualification
  remains incomplete.
- Support unencrypted SoundCloud fragmented-MP4 AAC HLS with validated
  manifests and indexed seeking; mixer playback, transport and EOF advancement
  passed. Artwork was unavailable in the HLS smoke.
- Spotify and live audio remain unsupported.

## Unreleased — Twitch and Kick providers

- Version-pinned OBS 32.2.0/33.0.3 SDK matrix across Linux, Windows and
  both macOS architectures, with version-specific packaging and SDK
  patches. OBS 32 local regression and both package layouts verified;
  actual OBS 33 native build/playback qualification pending.

- Public Twitch live channels, clips and completed VODs, sharing the existing native
  decoder, mixer routing and mixed-provider queue.
- Provider-specific URL/extractor and CDN validation, canonical Twitch
  channel/clip identities, bounded HTTPS preflight and live HLS checks.
- Source name becomes Streaming Media; the persisted source ID is unchanged.
- Verified isolated Linux OBS playback for a public live channel and clip.
  Offline polling and Twitch/Kick audio-only mode remain future work.
- Reuse normalized extractor metadata for the current item and enrich
  visible Twitch queue items with independent bounded lookups, including
  thumbnails, channel/category, duration, optional counts and details.
- Completed Twitch VODs: validated finite TS/fMP4 HLS snapshots, relative
  timeline, native TS seeks and segment-indexed fMP4 seeks. Real nine-hour
  recordings tested in both formats, including EOF-driven queue advancement.
- Real live/clip cross-queue smoke tests confirmed current and queued
  thumbnails and metadata while mixer audio continued.

- Kick live channels, completed UUID VODs and clip URLs use exact extractor
  routes, provider-specific CDN validation, normalized finite HLS including
  explicit byte ranges, and shared native transport and queue metadata.
- Kick live concurrent viewer counts and clip thumbnails when provided;
  unavailable live/recording thumbnails remain absent. Clip duration
  reconciliation handles a bounded segment-padding discrepancy. Indexed
  Kick VOD seeking and actual clip/VOD natural-end advancement verified.

- Generate current-media previews from the existing native decoder,
  using bounded aspect-preserving readback and reusing captured stills
  for matching queue entries without another media download.

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
