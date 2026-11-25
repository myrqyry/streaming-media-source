# Streaming Media Source for OBS

Native OBS video and audio sources for browser-free internet media playback. Paste a
public YouTube video, Twitch or Kick live channel, clip or VOD URL into
**Sources → Streaming Media**, or use the dock to import a public YouTube playlist.
The original URL, not an expiring media URL, is stored in scene settings.
Playback uses OBS's existing FFmpeg media engine; there is no browser,
desktop capture, listening port, or full-video download.

<img src="docs/streaming-media-dock.png" alt="Streaming Media dock with video playback, visual queue and bundled media engine" width="308">

*Version 0.5.1 in OBS 32.2 with the Colorway Fluffy Dark theme, using a disposable demo scene.*

[Get started](#install-a-local-test-package) · [Playback dock](#playback-dock-02) · [Import playlists](#import-youtube-playlists-060) · [WebSocket requests](#websocket-video-requests-03) · [Verification](#verification) · [Troubleshooting](#troubleshooting)

> **Development preview (0.6.0).** Linux x86_64 and native OBS 32.2 are the current
> locally exercised target. Public release packages are not available.
> [Build checks](https://github.com/myrqyry/streaming-media-source/actions) · [Release readiness](RELEASE_CHECKLIST.md) · [Changelog](CHANGELOG.md)

## Experimental TikTok video route

This development branch recognizes full public TikTok post URLs
(`https://www.tiktok.com/@creator/video/...`) in **Streaming Media** only.
It selects combined H.264/AAC progressive MP4 with known dimensions on
the two observed TikTok video hosts, using the shorter image edge for
the resolution limit on portrait posts. No live channels, short-share
redirects, photo posts, private posts, DRM or arbitrary CDN URLs.

**Not yet qualified for playback.** The pinned extractor returned metadata
and combined MP4 renditions for public samples, but media CDN requests
returned HTTP 403 even with the extractor's own HTTP headers. The isolated
OBS playback test failed with a controlled resolution error and no audio
or video. No browser fallback, cookie access or access-control workaround
is included. TikTok is not a supported provider until real playback,
transport, metadata and queue advancement pass.

## Streaming Audio

Add **Sources → Streaming Audio** for audio-only playback, and select that
source in the existing Streaming Media dock. It shares the native OBS mixer,
queue, transport controls, artwork, metadata and automatic advancement, but
does not request video renditions or expose video resolution controls.

| Provider | Supported input |
| --- | --- |
| YouTube | Public video URLs with dedicated HTTPS audio; YouTube playlist import |
| SoundCloud | Public tracks with progressive MP3 or supported unencrypted AAC HLS; public set import |
| Bandcamp | Public tracks with a standard MP3 streaming rendition; public album import |
| Spotify | Not supported: no direct full-track audio stream is exposed by its official playback API |

Audio extraction validates exact provider identity, HTTPS CDN hosts, dedicated
audio formats and reachable renditions. It does not use cookies, download
purchased albums, circumvent access restrictions or fall back to video decoding.
Use Play to select the first imported entry and prepend the rest, or Queue
to append without interrupting playback. Imports are bounded to 100 upcoming
entries and preserve the existing queue. Unsupported URLs are skipped.
Flat collection metadata may not reveal DRM, availability or audio format
until a track is resolved for playback. Remove unplayable entries or select
the next track manually; automatic skipping on resolution failure is not
qualified. Live audio and additional CDN coverage remain future work.
Only stream music for which you have the necessary rights.

Real Linux OBS 32.2 smoke tests passed for YouTube, SoundCloud and Bandcamp,
including mixed-provider automatic advancement, artwork, pause, seek, resume,
mixer audio and zero rendered video frames. Cross-platform playback, OBS 33,
audio recovery after interruption and live audio are not yet qualified.
Real audio-source imports of an eleven-track Bandcamp album and a four-video
YouTube playlist passed queueing, cancellation, metadata and first-track
playback. SoundCloud set imports queued supported URL entries, but full
collection playback and metadata tests did not pass. One sample began with a
DRM-protected track. Another contained unsupported API-only track URLs.
Unencrypted SoundCloud AAC HLS playback, indexed seeking, natural EOF and
advancement to Bandcamp passed; the full smoke failed its artwork assertion.
Remote requests in audio mode are not yet separately qualified.

## Requirements

- OBS Studio 32.x, native Linux installation (development/verification target:
  OBS 32.2.0, libobs 32.0.1 headers, Qt 6.4.2). OBS 33.0.3 is a separate
  qualification target; see the [OBS version compatibility guide](docs/ci.md#obs-32-and-33-compatibility).
- The bundled Linux x86_64 package includes the standalone yt-dlp
  extractor and Deno JavaScript runtime. No separate Python, Node,
  yt-dlp, curl executable or PATH configuration is required.
- Source-only builds without the bundle automatically discover a complete
  yt-dlp + Deno or Node installation on PATH, in ~/.local/bin or in the
  existing NVM layout. The same Qt HTTPS rendition validation is used.
- CMake and Qt6 Core/Widgets/Network and libobs/frontend API development
  headers for building.

### OBS version and install layout

Build against the SDK matching your OBS major version. OBS 33 requires a newer
build environment (Ubuntu 26.04 with FFmpeg 8 or newer for Linux CI); do not
replace system libraries on an OBS 32 workstation to build it. A module
compiled against OBS 33 must not be installed in OBS 32.

| Host | OBS 32 package | OBS 33 package |
| --- | --- | --- |
| Linux | `obs-streaming-media/bin/64bit/obs-streaming-media.so` under `$XDG_CONFIG_HOME/obs-studio/plugins` (default `~/.config`) | `obs-streaming-media/obs-streaming-media.so` under `$XDG_DATA_HOME/obs-studio/plugins` (default `~/.local/share`) |
| Windows x64 | `obs-streaming-media/bin/64bit/obs-streaming-media.dll` | `obs-streaming-media/obs-streaming-media.dll` |
| macOS | `obs-streaming-media.plugin` | `obs-streaming-media.plugin` |

Windows per-plugin packages belong under `%ProgramData%/obs-studio/plugins`;
macOS uses the standard OBS plugin bundle directory. Preserve the accompanying
`data/` directory (bundle resources on macOS). Do not install both layouts
for the same plugin at once. Close OBS before replacing an installation.
OBS 33 packages and these paths are prepared for qualification, not a claim
of verified playback. See [CI and package verification](docs/ci.md).

## Media engine setup

The module-only build can use user-installed official executables without
PATH configuration. In the Streaming Media dock, open the media engine
menu, download yt-dlp and Deno, open the runtime folder, put the two
executables there, and refresh engine status.

See the [runtime setup guide](docs/runtime-setup.md) for direct downloads,
SHA-256 verification and step-by-step instructions. Linux x86_64 is the
verified playback target; Windows and macOS runtime playback is not yet qualified.

## Install a local test package

First generate the Linux x86_64 bundled archive using the [build and install](#build-and-install)
instructions below (the example is for OBS 32). From the directory containing the archive and its checksum,
verify the archive, close OBS, and extract it into the per-user plugins directory:

```sh
sha256sum -c streaming-media-source-0.6.0-linux-x86_64-bundled.tar.gz.sha256
mkdir -p "$HOME/.config/obs-studio/plugins"
tar -xzf streaming-media-source-0.6.0-linux-x86_64-bundled.tar.gz \
  -C "$HOME/.config/obs-studio/plugins"
```

Restart OBS, add **Streaming Media**, and open **Docks →
Streaming Media**. The archive contains the plugin, extractor, Deno and
runtime notices. No separate runtime setup or system-wide installation is
required. Public binary releases are not yet available.

## Build and install

```sh
# Maintainers: prepare verified upstream runtime executables (Python 3.11+).
python3 scripts/prepare-runtime.py --output build/bundle/data/runtime --cache build/runtime-downloads
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DSTREAMING_MEDIA_RUNTIME_DIR="$PWD/build/bundle/data/runtime"
cmake --build build -j2
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.config/obs-studio/plugins/obs-streaming-media"
```

The commands above target OBS 32. For an OBS 33 SDK, use a separate build
directory, select `-DSTREAMING_MEDIA_PACKAGE_LAYOUT=modern` and stage under
`"${XDG_DATA_HOME:-$HOME/.local/share}/obs-studio/plugins/obs-streaming-media"`.
The default `auto` layout follows the detected libobs version; CI explicitly
selects the corresponding layout. Verify the build against its intended
host before installing.

The bundle is installed under the module's own `data/runtime/` directory.
It is preferred over system tools. For a source-only development build,
configure with `-DSTREAMING_MEDIA_RUNTIME_DIR=` and supply an existing
yt-dlp plus supported JavaScript engine. The install is local; no packages
are installed globally. Runtime pins are intentionally explicit and
downloaded assets are checked against recorded upstream SHA-256 digests.
The dock's media engine menu offers **Check for extractor updates** and
**Use bundled extractor**. Updates are fetched only on request from the
official stable yt-dlp release, verified against the release asset SHA-256
and size, and version-checked before activation. The updated executable
is stored in the OBS plugin configuration directory; the original bundle
remains untouched. The next resolution uses the new engine without an OBS
restart. Use the bundled extractor to roll back. Deno remains pinned.
See [third-party runtime notices](third-party/README.md) and the release
checklist before distributing binaries.

To generate a local package after the build, use:

```sh
cpack --config build/CPackConfig.cmake -B build/packages
```

CPack produces a compressed archive and SHA-256 file. The system-runtime
build produces a separately named archive without the bundled executables.
Do not upload bundled archives until the redistribution review is complete.

Restart OBS when convenient. In source properties, paste a supported YouTube, Twitch or Kick URL and
select maximum resolution, hardware decoding, playback speed and network
buffer. The source has OBS's normal media controls. Restart resolves a
fresh media URL; expired URLs and extraction errors can require a fresh resolution.
Quality is a cap, not a guarantee: when separate renditions are unavailable,
the resolver may select a lower-quality combined video/audio stream.

## Playback dock (0.2)

Open **Docks → Streaming Media**. Choose an existing Streaming Media source
by name; the picker follows its stable UUID, not the current scene item name.
Paste a video link and select Play or Queue. The current thumbnail, title,
actual decoded resolution, state, timeline and transport controls are shown
without a browser. The queue is stored per source in the scene collection.
Double-click an item to play it, or use the arrows and remove button.
Auto advance is off by default; when enabled it advances only after an
observed natural VOD end. Stop leaves upcoming entries intact. A premature
decoder end or error preserves the queue and surfaces Retry, which resolves
a fresh URL. Enable or disable automatic recovery in source properties
(**Recover interrupted playback**, enabled by default).

### Twitch channels, clips and VODs

Paste a public channel URL (for example, `https://www.twitch.tv/lofigirl`)
or a public clip or recording URL into the same source or dock. Both the
`clips.twitch.tv/<slug>` and `www.twitch.tv/<channel>/clip/<slug>` forms
are accepted. Channel names are case-normalized and clip aliases
canonicalized. Public completed recordings use
`https://www.twitch.tv/videos/<numeric-id>`; time query parameters are
currently discarded when canonicalizing the URL. Play, Queue and opt-in WebSocket requests share the same
mixed-provider queue. A live channel does not auto-advance like a finite
video; stop or choose the next item manually. Live seeking is disabled.
Current Twitch and YouTube playback reuses extractor metadata: title,
channel, category, description, publication date, reported resolutions,
and available views, concurrent viewers, likes, follower and comment
counts. Clip uploader is distinguished from channel. The dock shows a
compact channel/category line and escaped detail tooltips. Twitch
thumbnails load from the observed Twitch CDN with HTTPS-only requests,
no redirects, bounded size and image dimensions.

Visible Twitch queue entries are enriched one at a time by an independent
metadata-only yt-dlp process with a 20-second timeout and 4 MiB response
limit; this never downloads the media file or changes playback. YouTube
queue rows retain their lightweight oEmbed/thumbnail path. Previously
resolved items reuse session metadata. Counts are extraction-time
snapshots, not live polling, and missing fields are omitted. Failed
lookups fall back to URL labels; metadata is not persisted in scenes.
An offline channel requires a later retry.

Completed public Twitch VODs support HLS MPEG-TS and fragmented MP4.
The resolver validates a bounded finite media playlist, resolves segment
and initialization URLs against the validated CDN, and passes a private
playlist snapshot to the native decoder. It does not download the recording.
MPEG-TS uses native seeking with a sampled timestamp origin; fragmented
MP4 uses indexed segment seeking, reopening the requested segment rather
than scanning the recording. Segment-aligned seeks can land before the
requested time and briefly reopen playback; the timeline is not frame-exact.
Public VODs use the same queue, metadata, duration and natural-end auto
advance as other finite media. Continuous playback across timestamp wraps,
interrupted-VOD recovery and unusual HLS directives are not fully qualified.

The original OBS source identifier is unchanged, so existing scenes
continue to use the same source type. Twitch channel collections,
authenticated content, automatic offline waiting and audio-only playback
are not yet supported.

### Kick channels, clips and VODs

Paste a public Kick channel URL (`https://kick.com/<channel>`), a public
completed VOD (`https://kick.com/<channel>/videos/<uuid>`), or a clip
(`https://kick.com/<channel>/clips/clip_<id>`) into the source or dock.
These share the existing mixed-provider queue and metadata cache.
Live channel resolution uses the dedicated Kick extractor and the observed
live-video.net CDN; completed recordings and clips use finite validated HLS
snapshots. Clips use explicit byte-range segments rather than progressive
MP4 files, and some have no advertised resolution or codec metadata;
their available single rendition can play regardless of the configured
maximum resolution when the extractor does not report a height.
Completed Kick VODs use segment-indexed seeking, retaining the full recording
duration while reopening the containing segment. Seeks are segment-aligned,
not frame-exact. The finite HLS timeline normalizes nonzero timestamps; for Kick clips,
a positive reported duration may replace a decoder duration only when the
difference is at most three seconds of trailing segment padding.

The dock shows title, channel, category, description, available qualities,
views and concurrent viewer count when supplied by the extractor. Counts
are snapshots, not live polling. Clip thumbnails are supported; sampled
live thumbnail requests returned HTTP 403 and a sampled completed VOD
had no thumbnail. The dock falls back gracefully rather than bypassing
access restrictions or inventing data.

Only public content is supported. Ongoing VODs are rejected in favor of the
live channel URL. Offline channels, inaccessible media, channel-wide
video/clip imports and authentication are not supported by this slice.
The observed CDN allowlist deliberately does not admit arbitrary
CloudFront or other unrelated media hosts.

Use `SMOKE_KICK=live`, `SMOKE_KICK=clip` or `SMOKE_KICK=vod` with
the corresponding public URL in the isolated smoke harness. Set
`SMOKE_KICK_NEXT` to an accessible public media URL for queue metadata;
`SMOKE_KICK_EOF=1` additionally exercises natural completion and next
item playback for a finite item. Live and archived media availability
can change independently of the plugin.

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

### Visual playlists (0.4.1)

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

### Import YouTube playlists (0.6.0)

In YouTube, copy a public playlist link (for example,
`https://www.youtube.com/playlist?list=...`). Open **Docks → Streaming Media**,
select the source you want to control, and paste the link into the existing URL field.

Choose the action that matches what you want to happen:

- **Queue** appends available videos in playlist order, preserving the
  current video and the existing auto-advance setting.
- **Play** selects the first available video, prepends the rest ahead of
  existing upcoming videos, and enables Auto advance. Existing queued
  entries are never evicted.
- A video URL with a supported playlist ID offers **This video** or
  **Entire playlist**. Dynamic mixes and authenticated playlists are
  outside the supported scope.
- An independent asynchronous yt-dlp flat-playlist request enumerates
  metadata without resolving each item's actual media streams. It examines
  at most 101 entries per import, with a 45-second deadline and 4 MiB
  response limit. The queue still holds at most 100 upcoming videos.
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

The queue limit is shared with existing individual videos. Large playlists are
truncated rather than paginated; importing the same playlist again may add
duplicates. Shuffle and repeat are not yet implemented.

**Verified in 0.6.0:** the bundled engine imported a public Blender uploads
playlist with an empty PATH and a fresh HOME. Queue added 99 entries alongside
one existing entry without interrupting playback. Play selected the first video,
preserved the existing queue, enabled automatic advancement, and restored actual
video and mixer audio. Cancellation and the normal playback regression also passed.

### Playback recovery (0.4)

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

### Theme integration (0.3.1)

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

## WebSocket video requests (0.3)

Uses OBS WebSocket 5.x's existing authenticated server and
`CallVendorRequest`, vendor name `streaming-media`. No additional server,
port, or automatic WebSocket configuration changes. OBS WebSocket API
headers are needed at build time; local playback still works if the runtime
WebSocket module is absent.

Enable **Accept WebSocket video requests** in the target source properties.
This is off by default. Only authenticated, trusted integrations should have
OBS WebSocket credentials. Do not expose the OBS control socket to viewers:
a chat bot should enforce its own request permissions, rate limits and
moderation before sending requests. Requests only append to the upcoming
queue. If auto-advance is enabled, those entries can subsequently play.

| Vendor request | Request data | Response data |
| --- | --- | --- |
| ListSources | none | status, sources (sourceUuid, sourceName, acceptRequests) |
| SubmitVideo | sourceUuid, url | status, requestId if pending |
| GetRequestStatus | sourceUuid, requestId | status |
| GetQueue | sourceUuid | status, currentUrl, items, acceptRequests, autoAdvance |

Example using an already authenticated obs-websocket-js client:

```js
const { responseData: sources } = await obs.call("CallVendorRequest", {
  vendorName: "streaming-media", requestType: "ListSources",
});
const target = sources.sources.find(s => s.acceptRequests);
if (!target) throw new Error("No enabled Streaming Media source");
const { responseData: receipt } = await obs.call("CallVendorRequest", {
  vendorName: "streaming-media", requestType: "SubmitVideo",
  requestData: { sourceUuid: target.sourceUuid, url: "https://youtu.be/jNQXAC9IVRw" },
});
```

A `pending` receipt means the source accepted the command for processing;
poll `GetRequestStatus` using its requestId and the same sourceUuid to confirm
`queued`, `duplicate`, `queue_full`, `disabled` or `invalid_url`.
Immediate failures also include `busy`, `source_not_found`, and
`unavailable`. An expired or unknown receipt returns `unknown_request`.
Receipts are bounded in memory and not persisted. Queue entries are persisted
as before. Canonical video IDs deduplicate watch/short links and exclude the
current video; the remote interface does not accept arbitrary media URLs or
commands to replace what is playing. Queue limit: 100; pending commands: 64.

`tests/websocket-smoke.py` exercises a disposable OBS profile with an empty
Streaming Media source and a password-protected WebSocket server. Pass the
isolated profile's obs-websocket config.json path; never use the active
production profile for this smoke (it changes that source's test settings).

### Playback lock safety (0.3.2)

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
private temporary directory. FFmpeg reads the segments on demand. A private
OBS Media Source renders video and forwards audio into the parent source;
the child's own output mixer is disabled to avoid double routing.

## Verification

The [CI workflow](.github/workflows/ci.yml) covers two pinned SDK versions,
OBS 32.2.0 and 33.0.3, across Linux x86_64, Windows x64, macOS Intel and
macOS Apple Silicon. It checks deterministic regressions and module-only
package contents and checksums. The matrix is a qualification mechanism,
not a guarantee that every cell has passed. It does not bundle runtime
executables, run live playback or publish releases.

| Target | Verification boundary |
| --- | --- |
| Linux x86_64 / OBS 32.2 / Qt 6.4.2 | Local native tests, isolated real playback and bundled package smoke |
| Ubuntu 24.04 / pinned OBS 32.2 SDK | Combined provider branch local Release build, nine tests and package gate passed |
| Windows x64, macOS Apple Silicon / Intel / OBS 32.2 | [Merged CI baseline passed](https://github.com/myrqyry/streaming-media-source/actions/runs/45528453220); newer provider changes still need native CI and real playback |
| OBS 33.0.3 / all four platforms | SDK pins and matrix configured; actual native build and playback qualification pending |
| Flatpak, other OBS majors | Not qualified |

See the [CI guide](docs/ci.md) for local reproduction and the [release
checklist](RELEASE_CHECKLIST.md) for remaining gates. The optional real
playback smoke below is separate from deterministic CI.


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

## Troubleshooting

| Symptom | First check |
| --- | --- |
| Source is missing | Confirm the module was built for your OBS version and is in the corresponding [install layout](#obs-version-and-install-layout). Restart OBS after installation. |
| Media engine missing | Follow the [runtime setup guide](docs/runtime-setup.md) or use a compatible existing system runtime. |
| Video no longer resolves | Check for extractor updates in the dock, then retry. |
| Update breaks resolution | Choose Use bundled extractor in the engine menu and retry. |
| Playback stalls or has rendering issues | Try a lower resolution or disable hardware decoding; include the OBS/plugin versions and redacted logs in a bug report. |
| Playlist import adds fewer videos than expected | Check the 100-entry upcoming queue limit, inaccessible entries and the 101-entry enumeration bound. |
| Playlist import cannot start | Use a supported public playlist link, check the media engine, or try the individual video. |
| Queue duration is incomplete | Some entries lack known durations; imported durations are not restored after restarting OBS. |
| Remote requests are rejected | Enable Accept WebSocket video requests on the intended source and use its UUID. |

FFmpeg and OBS logs may contain expiring signed media URLs. Redact them,
credentials, private scene information and local paths before sharing.

## Scope and limits

The intended experience is direct media playback without the web player's
ad UI. Ads, in-video sponsor segments, DRM, private/member-only videos,
live-stream availability and platform restrictions are not guaranteed to be removed or
supported. No browser-cookie import, login, proxy, token-provider integration
or automatic circumvention of unavailable media. Use content you have
permission to play or rebroadcast. YouTube extraction can change independently
of the plugin. Current verified scope is public YouTube VOD, sampled public
Twitch and Kick live channels/clips and completed VODs, native Linux OBS and software decoding; hardware decoding is an option but is not yet qualified.

See ACTIVE_WORK.md for exact runtime evidence and remaining work.

## License

Original project code and documentation: [GPL-3.0-or-later](LICENSE).
Third-party components retain their own licenses. See the
[licensing and distribution guide](docs/licensing.md) and
[third-party runtime notices](third-party/README.md).

## Reference

- https://github.com/yt-dlp/yt-dlp
- https://github.com/yt-dlp/yt-dlp/wiki/EJS
- https://github.com/yt-dlp/yt-dlp/wiki/PO-Token-Guide
- https://github.com/obsproject/obs-studio
