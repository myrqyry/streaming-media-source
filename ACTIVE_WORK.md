# Active work
Outcome: YouTube-first native OBS media source with reliable actual picture and
audio, ordinary transport controls, and no browser or full download.
Checkpoint: real isolated libobs smoke on yuzu-mint passed at 1080p (H.264/AAC
adaptive HLS). PLAY 1920x1080, 26 nonblack frames, 40 non-silent source-audio
blocks, 42 mixed-audio blocks, time 1068 ms, duration 634566 ms.
CONTROL pause=1 seek=1 resume_audio=1, time=50083 ms; process exit 0.
A preceding real smoke also passed combined 360p fallback with all controls.
Root cause of previous silent 1080p: chained QString::arg substituted percent
escapes in signed audio URL, so validated URL differed from playback URL.
Regression was observed red then green. HLS unknown audio codec appears null
in yt-dlp JSON; explicit video=none identifies candidate audio. Bounded curl
probe chooses accessible renditions; VOD media playlist snapshots are private.
Locked: standalone, no browser, no SRT relay, no whole-video cache, no secrets
in saved scene settings; keep OBS playback owner.
Remaining: hardware decode qualification, repeated samples and long sessions,
automatic recovery from expired URLs, source lifecycle/rapid switch and
multi-source tests, user-facing error/quality diagnostics, playlist/dock UX,
live qualification. No universal ad-removal or broadcast-rights claims.

Repeat on final formatted/hardened build: 1920x1080, 25 nonblack frames, 38 source and 40 mixed audio blocks; pause/seek/resume all passed, exit 0.

## 0.2.0 — dock and queue (2026-11-23)
Verified in isolated libobs: 1920x1080 adaptive playback, 25 nonblack frames,
38 source and 41 mixed audio blocks; pause/seek/resume all 1. Real Qt widget
source picker and decoded YouTube thumbnail passed; actual widget buttons
added, reordered, removed and advanced the queue into a second playback.
Seek near EOF followed by real decoder completion advanced to the next URL
with non-silent audio, AUTO_NEXT 1. Stop preserved queued entries. Source
and private FFmpeg child destroyed before core shutdown; canvas-owned scene
released during shutdown; process exit 0. Separate isolated full OBS 32.2
profile registered the Streaming Media dock and source and shut down;
unattributed 3 global OBS leaks (also seen in other isolated OBS profiles).
Tests 2/2. No active user OBS changes; installed v0.1.0 retained.
Locked: queue changes serialized in source tick; source owns advancement,
not the dock timer; original URLs only in persisted settings; validated
thumbnail ID and bounded fetch; explicit opt-in auto advance.
Remaining: automatic bounded fresh-URL recovery with resume, richer queue
metadata/thumbnail cards, hardware decode and multi-source qualification.

## 0.3.0 — WebSocket request intake (2026-11-23)
Vendor streaming-media registered through obs_module_post_load, using the
existing OBS WebSocket 5.7.4 RPC 1 server, not a second listener. Per-source
remote_requests opt-in defaults false. SubmitVideo takes an explicit source
UUID and a YouTube video URL; source tick owns admission and persists canonical
queue entries. Bounded receipts distinguish pending/queued/rejected; remote
admission rejects duplicates including current video, invalid URLs and full
queues. No immediate-play or arbitrary command endpoint. Real authenticated
WebSocket smoke against separate OBS 32.2 profile passed source discovery,
disabled default, wrong target, invalid URL, pending-to-queued receipt,
duplicate, queue snapshot, no autoplay, and disable. CTest 2/2. Isolated
test instance stopped. Existing user OBS configuration and installed v0.1.0
remain untouched. Next: Chatbus adapter/moderation and request attribution
if desired, automatic stream recovery, richer queue metadata, hardware
decode and multi-source qualification. No claims of viewer authentication
or public request server.
