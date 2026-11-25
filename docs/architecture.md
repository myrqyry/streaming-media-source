# Architecture and live verification

[Back to overview](../README.md)

## Playback lock safety

The private decoder's audio capture callback does not call the parent
source. It copies PCM and its original timestamp into a preallocated,
bounded handoff queue. An independent consumer delivers the audio outside
the decoder and queue locks. Source changes invalidate pending PCM. Video
dimensions are sampled on the video tick and exposed atomically; the dock
reads the snapshot rather than acquiring nested source filter locks.
Decoder updates must be applied before accepting new playback output.
This removes the lock inversion identified in a production freeze stack.

## Architecture

The bundled or system yt-dlp runs on a cancellable worker with explicit time/size bounds and without
loading user config. The resolver uses Qt HTTPS with bounded response sizes, deadlines and provider-specific CDN/redirect validation (no external curl process). It validates reachable video and audio
renditions before choosing them, preserves signed URLs verbatim, and for
supported VOD HLS playlists saves only the validated playlist metadata in a
private temporary directory. FFmpeg reads the segments on demand. TikTok uses a validated complete temporary MP4 from the bundled downloader instead; see [provider details](providers.md). A private
OBS Media Source renders video and forwards audio into the parent source;
the child's own output mixer is disabled to avoid double routing.

## Isolated playback smoke

Run from the repository root on Linux with a matching OBS installation. This exercises real network playback independently of the [deterministic CI matrix](ci.md).

```sh
xvfb-run -a timeout 200 build/obs-youtube-smoke \
  "$PWD/build/obs-streaming-media.so" \
  'https://www.youtube.com/watch?v=aqz-KE-bpKQ'
```

For a staged bundled build, set `SMOKE_MODULE_DATA` to the staged
`data/` directory and point the test at the staged module binary.
The test can run with an empty PATH and a fresh HOME to verify runtime
independence.

This network-dependent isolated libobs test checks actual rendered pixels,
nonzero source and mixed audio, pause, seek and resume; actual Qt widget thumbnail, queue editing, next playback, natural EOF auto advance, stop preservation and teardown. It is intentionally
separate from the deterministic CTest suite. To exercise recovery, run
the same command with `SMOKE_RECOVERY=1` in the environment: this stops
the actual private FFmpeg decoder mid-video, checks fresh resolution,
position and mixer audio recovery, and cancels a second scheduled retry
using the parent Stop action. This is an injected decoder interruption,
not a simulated network outage. Set `SMOKE_PLAYLIST_IMPORT=1` to exercise
real public playlist enumeration, cancellation, append while playing,
capacity limits, metadata, Play and source teardown. Set
`SMOKE_PLAYLIST_DRAG=1` for the
real X11 drag-reorder test in the normal playback smoke (requires Python
Xlib). It validates that public titles and thumbnails load and that the
actual persisted queue order changes. For Twitch, set `SMOKE_TWITCH_LIVE=1` and pass a live channel URL, or
`SMOKE_TWITCH_CLIP=1` and pass a public clip URL. These modes check real
video/audio, current title/author and thumbnail, metadata and thumbnail
of a different queued Twitch item, continued mixer audio, stop and source
teardown; the clip mode additionally exercises transport. Set
`SMOKE_TWITCH_VOD=1` and supply a public completed VOD URL to test
transport, queue metadata and an actual near-end seek and natural
auto advance to a second public VOD. The two sampled recording IDs in
the harness may expire; set `SMOKE_TWITCH_VOD_NEXT` to a different accessible
public recording to replace the default next item. Test logs from FFmpeg may contain
temporary signed media URLs; redact before sharing.

Use `SMOKE_AUDIO=1` for the audio source, and `SMOKE_GENERATED_PREVIEW=1` to exercise captured thumbnails. See [provider details](providers.md) for additional Kick modes.
