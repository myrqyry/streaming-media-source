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

## 0.3.1 — theme integration (2026-11-23)
Native OBS icon class mapping, state-only play/pause icon updates, coalesced
theme/style/font/palette refresh, inherited typography, adaptive preview size
and DPR-aware thumbnail scaling. Theme regression observed red on old dock
(native class missing), green after implementation; 3/3 CTest. Real OBS
32.2 disposable profile opened the dock with the actual Colorway Fluffy Dark
theme; screenshot reviewed (native icons, text, inputs, sliders, queue).
Isolated OBS exited successfully. No production theme/profile edits or
replacement of installed binary. Previous production crash investigation
remains unresolved: 20:11 log ended after media resolution and preview spacing
labels; exact saved video passed standalone hardware-enabled smoke, while
an earlier distinct 20:07 crash recorded an Aftermarket fault.

## Production crash recurrence (2026-11-23 21:02 CST)
Production log 20-50-11: installed v0.3.0, source restored, UI Augment audio
envelope attached, combined VOD resolved 20:50:23, final log 20:50:44 after
four native OBS Preview spacing labels. Cinnamon removed the OBS tray icon
20:54:59; no corresponding kernel fault or core recorded. This permits a
hang/termination interpretation but does not establish either mechanism.
Correct earlier attribution: 20:07 Aftermarket core module was in a disposable
/tmp/aftermarket-layout-obs-smoke profile, not the production plugin path.
20:19 and 20:22 cores show libobs X11 hotkey thread null reply dereference
(offset e962e, obs-nix-x11.c key_pressed) during Qt application termination;
not sufficient to explain latest production incident. Theme v0.3.1 remains
uninstalled. No production profile or binary edits.
Isolated full OBS 32.2 with installed v0.3.0, installed Aftermarket and saved
YouTube URL/default hardware setting: authenticated GetMediaInputStatus
responded 15/15, playback cursor progressed to 19833 ms, shutdown logged
Freeing OBS context data. Not a reproduction of production crash: Xvfb,
minimal scene, no production envelope attachment or full plugin/filter graph.
Next discriminating evidence: whether production UI froze versus vanished;
capture live all-thread stack on a hang before exit, or use matching core
on spontaneous crash; test production source with HW off as a reversible
A/B without removing saved source. No root cause or fix claimed.

## Confirmed freeze (2026-11-23 21:14 CST)
User confirmed UI became unresponsive rather than disappearing spontaneously.
Latest 21:05 production launch failed rendition validation at 21:05:25, then
shut down normally at 21:12 with source and child destroyed. No OBS running
at investigation time. Yama ptrace_scope=1: no arbitrary debugger attachment
or security setting changes. Added scripts/obs-hang-debug.sh for a deliberate
GDB-parent launch of the existing OBS binary in a separate tmux server;
start refuses if OBS is already running, capture interrupts and collects
all thread stacks, resume explicitly continues. Private log with frame args
omitted. Fixture self-test using sleep passed actual GDB interrupt/snapshot.
No OBS launch or install performed. Next: launch via script when user wants,
then capture while frozen; investigate blocked thread/lock ownership.

## 0.3.2 — production freeze fix (2026-11-23)
Actual frozen process PID 254458, 21:59:04 final log. User collected GDB
all-thread backtrace at 22:03; stored privately at
~/.local/state/obs-hang-diagnostics/frozen-threads.txt. Relevant stacks:
LWP 254458 main thread MediaDock::refresh -> obs_source_get_width;
LWP 254473 graphics thread nested obs_source_get_width; LWP 257982 decoder
obs_source_output_audio -> Source::audio -> obs_source_output_audio.
libobs implementation holds source filter_mutex through audio capture
callbacks and while invoking get_width. Parent size callback called child
get_width; decoder capture called parent audio output. Opposite lock order.
Fix: bounded preallocated PCM handoff, independent consumer, generation
invalidation, no sink invocation while queue locked; atomically sampled
decoder dimensions, dock reads status snapshot. Actual deferred decoder
update signal gates output admission; child EOF signal captured atomically
so decoder reconnect cannot hide terminal state. No external publication.
Tests: first stress completed 68421 dimension queries, 1080p picture/mixer
audio, controls, dock, next playback, teardown; EOF auto-advance failed.
Second stress completed 62306 queries; stop preserved queue; EOF failed,
logging premature terminal timestamp 23 ms. Identified update admission
race and adjusted near-end test to observe actual seek position.
Intermediate full OBS 32.2 isolated profile with Aftermarket and actual saved
URL/default HW setting responded to 15/15 WebSocket health checks, playback
reached 21200 ms, clean process exit. Not full production environment.
CTest 4/4; audio handoff blocked-sink test 100 repeated passes.
EOF and final installation verification recorded below.

Final v0.3.2 regression: observed near-end seek at 626590 ms; decoder
reported natural end at observed watermark 634583 / 634566 ms despite
clock reset. VodProgress regression rejects stale position after backward
seek. Final real libobs smoke (deadlock-fix-smoke4.log) exited 0:
1920x1080, 25 nonblack frames, 38 source / 40 mixed audio blocks,
pause/seek/resume, dock thumbnail, queue edit/next/auto-next/stop preservation
all passed; 55493 concurrent dimension queries, source teardown passed.
CTest 4/4. Installed atomically after backup, without restarting active OBS.
Production retest still required on the full plugin graph.

## v0.4.0 — bounded playback recovery
Renamed remote now points to myrqyry/streaming-media-source, verified private.
Existing module name and source ID retained for scene compatibility.
Recovery enabled by default, configurable per source; two attempts per
selected video, one/three second backoff on the video tick. Fresh resolution,
resume VOD near the last observed position with output gated until seek
confirmation; live retry has no seek. Manual stop/restart/change cancels
scheduled recovery; queue remains intact. No initial extraction retry,
silent-stall watchdog or general provider expansion yet.
Deterministic recovery test red before implementation, then CTest 5/5.
Actual libobs smoke with SMOKE_RECOVERY=1 exited 0: forced unexpected stop
of private decoder at 50866 ms, fresh resolution and restored position/mixer
audio, second attempt cancelled by parent Stop, queue intact, 33895
dimension queries, source teardown. This was a decoder interruption, not
a real network outage. Normal live-network smoke exited 0: 1080p picture
and audio, pause/seek/resume, dock thumbnail, queue edit/next/auto-next,
stop preservation, 55848 concurrent dimension queries and teardown.
Previous v0.3.2 installed full OBS resize test: actual mouse source resize
1.0 to 2.035 and back to 1.002, window-border resize and additional window
size changes while playback continued; clean exit. Fit to Screen shortcut
not separately verified.
Public release possibility raised, not authorized. RELEASE_CHECKLIST.md
records licensing, history privacy, packaging, validation and documentation
gates. Repository remains private. No new install or push in this slice.
Next: visual queue metadata and compact playlist controls; public package
and screenshot/issue documentation after license/history review.
