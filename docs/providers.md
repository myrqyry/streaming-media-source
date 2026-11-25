# Provider details

[Back to overview](../README.md)

## TikTok video posts

**Streaming Media** accepts full public TikTok post URLs
(`https://www.tiktok.com/@creator/video/...`). TikTok's CDN rejected
direct HTTP playback in our tests, but the bundled yt-dlp downloader
successfully retrieved the owner's public test video. This provider
therefore buffers a complete temporary H.264/AAC MP4 before playback
through OBS's native FFmpeg source; it does not use a browser.

Downloads require a known file size up to 64 MiB, a validated format and
observed provider host, sufficient free disk space, and a bounded download
deadline. Cancellation and source changes discard stale results; temporary
media is retired after decoder updates and removed on normal teardown.
No persistent media library, cookies, private posts, DRM, live channels,
short-share redirects or photo posts are supported. Other providers retain
their existing streaming paths.

Verified on Linux OBS 32.2 using a public 576 × 576, 155-second post:
real video and mixer audio, pause/seek/resume, generated preview, queue
editing, YouTube-to-TikTok automatic advancement, stop and source teardown.
No temporary MP4 remained after the isolated test. Additional TikTok
posts, TikTok's own natural EOF advancement, cancellation mid-download,
crash cleanup and Windows/macOS/OBS 33 playback still need qualification.

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

## Twitch channels, clips and VODs

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

## Kick channels, clips and VODs

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
