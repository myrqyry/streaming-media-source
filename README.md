# Streaming Media for OBS

A standalone native OBS source for browser-free YouTube playback. Paste a
public YouTube video URL into **Sources → Streaming Media (YouTube)**.
The original URL, not an expiring media URL, is stored in scene settings.
Playback uses OBS's existing FFmpeg media engine; there is no browser,
desktop capture, listening port, or full-video download.

## Requirements

- OBS Studio 32.x, native Linux installation (development/verification target:
  OBS 32.2.0, libobs 32.0.1 headers, Qt 6.4.2).
- Current yt-dlp with its matching EJS components and a supported JavaScript
  runtime (Node >=22 or Deno); the source finds yt-dlp on PATH or in
  ~/.local/bin, and discovers Node in PATH or ~/.nvm.
- curl, used for bounded media rendition validation.
- CMake and Qt6 Core/Widgets/Network and libobs/frontend API development headers for building.

## Build and install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.config/obs-studio/plugins/obs-streaming-media"
```

Restart OBS when convenient. In source properties, paste a YouTube URL and
select maximum resolution, hardware decoding, playback speed and network
buffer. The source has OBS's normal media controls. Restart resolves a
fresh media URL; expired URLs and extraction errors can require a restart.
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
a fresh URL. Automatic resume/recovery is not yet implemented.

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

yt-dlp runs on a cancellable worker with explicit time/size bounds and without
loading user config. The resolver validates reachable video and audio
renditions before choosing them, preserves signed URLs verbatim, and for
supported VOD HLS playlists saves only the validated playlist metadata in a
private temporary directory. FFmpeg reads the segments on demand. A private
OBS Media Source renders video and forwards audio into the parent source;
the child's own output mixer is disabled to avoid double routing.

## Verification

```sh
xvfb-run -a timeout 200 build/obs-youtube-smoke \
  "$PWD/build/obs-streaming-media.so" \
  'https://www.youtube.com/watch?v=aqz-KE-bpKQ'
```

This network-dependent isolated libobs test checks actual rendered pixels,
nonzero source and mixed audio, pause, seek and resume; actual Qt widget thumbnail, queue editing, next playback, natural EOF auto advance, stop preservation and teardown. It is intentionally
separate from the deterministic CTest suite. Test logs from FFmpeg may contain
temporary signed media URLs; redact before sharing.

## Scope and limits

The intended experience is direct media playback without the web player's
ad UI. Ads, in-video sponsor segments, DRM, private/member-only videos,
live streams and platform restrictions are not guaranteed to be removed or
supported. No browser-cookie import, login, proxy, token-provider integration
or automatic circumvention of unavailable media. Use content you have
permission to play or rebroadcast. YouTube extraction can change independently
of the plugin. Current verified scope is public VOD, native Linux OBS,
software decoding; hardware decoding is an option but is not yet qualified.

See ACTIVE_WORK.md for exact runtime evidence and remaining work.

## Reference

- https://github.com/yt-dlp/yt-dlp
- https://github.com/yt-dlp/yt-dlp/wiki/EJS
- https://github.com/yt-dlp/yt-dlp/wiki/PO-Token-Guide
- https://github.com/obsproject/obs-studio
