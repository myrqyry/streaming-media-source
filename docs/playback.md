# Playback and queue guide

[Back to overview](../README.md)

## Playback dock

Open **Docks → Streaming Media**. Choose an existing Streaming Media or Streaming Audio source
by name; the picker follows its stable UUID, not the current scene item name.
Paste a media link and select Play or Queue. The current thumbnail, title,
actual decoded resolution, state, timeline and transport controls are shown
without a browser. The queue is stored per source in the scene collection.
Double-click an item to play it, or use the arrows and remove button.
Pause/Play immediately reflects the requested transport action and reconciles with the decoder state; rapid successive clicks no longer send duplicate pause commands based on stale dock polling. The dock preview remains a still image, not a live video monitor.
Auto advance is off by default; when enabled it advances only after an
observed natural VOD end. Stop leaves upcoming entries intact. A premature
decoder end or error preserves the queue and surfaces Retry, which resolves
a fresh URL. Enable or disable automatic recovery in source properties
(**Recover interrupted playback**, enabled by default).

### Generated playback previews

The dock captures its own still from the existing OBS media decoder when
video is available. A small aspect-preserving image (at most 320 × 180)
is rendered and read back over separate video ticks, once per playback
generation; it is not a continuous video preview. The captured image
takes priority over provider thumbnails and is reused for matching queue
entries in the bounded session cache. There is no extra media download,
browser, second decoder, or persistent screenshot file. A provider image
remains a fallback before video is available or if capture fails.
Unplayed queue items still use provider thumbnails when available.

### Visual playlists

Upcoming videos show a thumbnail, title and channel name, with full
metadata and original URL in the tooltip. Drag rows to reorder them,
or use the existing move buttons. Reordering is applied by the source;
a drag is discarded if the selected source or queue changed mid-gesture.

Public YouTube oEmbed metadata and fixed-host thumbnail requests are
asynchronous, bounded and cached in memory by video ID. Only visible
rows are requested, one at a time; unavailable metadata falls back to
the video ID without blocking playback. The delegate inherits the OBS
theme and selection palette. Queue entries remain original URLs in
scene settings.

### Import collections

Copy a public YouTube playlist link (for example,
`https://www.youtube.com/playlist?list=...`). Streaming Audio also accepts public SoundCloud sets and Bandcamp albums; see [provider details](providers.md) for limitations. Open **Docks → Streaming Media**,
select the source you want to control, and paste the link into the existing URL field.

Choose the action that matches what you want to happen:

- **Queue** appends available entries in collection order, preserving the
  current video and the existing auto-advance setting.
- **Play** selects the first accepted entry, prepends the rest ahead of
  existing upcoming videos, and enables Auto advance. Existing queued
  entries are never evicted.
- A video URL with a supported playlist ID offers **This video** or
  **Entire playlist**. Dynamic mixes and authenticated playlists are
  outside the supported scope.
- An independent asynchronous yt-dlp flat-playlist request enumerates
  metadata without resolving each item's actual media streams. It examines
  at most 101 entries per import, with a 45-second deadline and 4 MiB
  response limit. The queue still holds at most 100 upcoming entries.
- Cancel while importing to leave playback and queue unchanged. A source
  change cancels an in-flight enumeration; the resulting batch is applied
  to its original source, never the newly selected source.
- Known inaccessible entries are skipped; entries unavailable only at
  playback time may still fail later. The dock reports the number actually
  queued and capacity limits.
- Titles, channel names and known durations populate the in-memory metadata
  cache. Thumbnails still use the existing fixed-host fetcher. The queue
  duration summary covers upcoming videos, excludes the currently playing
  video, and identifies entries with unknown durations. URLs survive an OBS
  restart, but imported duration metadata is session-only. Reimporting a
  playlist adds entries; it is not a metadata-refresh operation.

The queue limit is shared across providers within each source. Video and audio sources maintain separate queues. Large playlists are
truncated rather than paginated; importing the same playlist again may add
duplicates. Shuffle and repeat are not yet implemented.

**Verified in 0.6.0:** the bundled engine imported a public Blender uploads
playlist with an empty PATH and a fresh HOME. Queue added 99 entries alongside
one existing entry without interrupting playback. Play selected the first video,
preserved the existing queue, enabled automatic advancement, and restored actual
video and mixer audio. Cancellation and the normal playback regression also passed.

### Resume after reopening OBS

Both Streaming Media and Streaming Audio have **Resume playback on OBS
startup** in source Properties, enabled by default. During OBS's normal
scene-collection save, the source records its last observed playback position
(in milliseconds) and requested transport state. On the next load it
resolves fresh media URLs and restores the position through the existing
VOD recovery/seek path. Paused playback stays paused; explicitly stopped
or naturally ended media stays stopped unless auto advance selected a new
item. The remaining queue is preserved by the existing scene settings.
Live streams reconnect rather than seeking to an old live timestamp.

The initial activation after restoring a bookmark does not invoke
**Restart on scene activation**; later activations still honor that setting.
Manual Restart and selecting another item start from the beginning.
Disabling startup resume restores the old startup behavior and clears the
saved bookmark on the next normal OBS save.

The bookmark is tied to the source UUID and selected URL using a digest;
renaming a source does not change it, but copying settings to another
source or changing the URL does not apply the old position. No resolved
stream URLs, credentials, or additional downloaded media are saved.
An older scene without a bookmark starts normally.

Restoration is approximate: provider seek accuracy, FFmpeg keyframes,
and indexed HLS segment boundaries can move the actual position slightly.
The restored paused path waits for the decoder clock to settle before
exposing the output. Normal closing/saving is supported; there is no
independent ten-second crash checkpoint journal yet, so forced termination
can lose progress since OBS last saved the collection. Startup recovery
cannot restore an unavailable or expired provider item.

Verified in separate isolated Linux OBS 32.2/libobs processes using a
public YouTube video and a dedicated audio rendition. The video bookmark
was restored paused, subsequently resumed actual video and mixed audio,
and remained stopped after a further reload. The audio source resumed
automatically with actual mixed audio. Other providers, live reconnection,
long-distance seeks, and the full GUI shutdown/reopen workflow have not
been separately qualified for this feature.

### Playback recovery

After playback has started, an unexpected decoder stop, early end or error
schedules a fresh resolution. A selected video has at most two automatic
attempts, with one- and three-second backoff delays. A recovered VOD seeks
back to its last observed playback position before admitting picture and
audio; live streams, where supported, reconnect at the live position.
A failed recovery resolution uses the remaining retry budget. Initial
extraction errors do not automatically retry. Stop, manual restart and
selecting another video cancel pending recovery; the upcoming queue is
not consumed by an interruption. The Retry button still restarts from
the beginning and grants a new retry budget.

Recovery is driven by decoder state changes; silent stalls without a
terminal state are not yet detected. A recovery may fail if the network
or provider remains unavailable.

### Theme integration

The dock uses OBS's native media and utility icon classes, theme-provided
typography, inherited widget palettes and style metrics. Its icons refresh
on OBS theme changes and Qt style/palette/font changes; playback polling
does not overwrite theme icons. Preview dimensions adapt to font size and
dock width, and the cached thumbnail is scaled at the device pixel ratio.
Ten-second seek buttons use explicit −10/+10 labels rather than ambiguous
playlist icons. The Qt fallback supports styles that lack OBS icon classes.

The deterministic dock-theme-test checks live stylesheet icon replacement
and responsive preview sizing without a live video. The actual dock has also
been visually inspected in a disposable OBS 32.2 profile with Colorway Fluffy
Dark. This does not qualify every third-party theme or display scale.
