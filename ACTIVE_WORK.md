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

## v0.4.1 — visual playlist rows and drag ordering
Native palette-aware thumbnail/title/channel delegate, bounded asynchronous
oEmbed and fixed-host JPEG fetching for visible rows only, 128-entry
in-memory ID cache, one queue request in flight, unavailable metadata
fallback. No yt-dlp subprocess per queue item. Original URL persistence
unchanged. Drag captures source/queue snapshot and sends existing move
command only if still current; native highlight insertion indicator.
Metadata tooltips escape HTML. Playlist parser and insertion-target
tests red before implementation, then CTest 6/6. Real libobs playback
smoke passed with titles and thumbnails for both queue entries, normal
move controls, EOF auto-next, 55420 dimension queries, clean teardown.
Real X11 drag smoke exited 0: PLAYLIST_METADATA 1, PLAYLIST_REORDER 1,
AUTO_NEXT 1, QUEUE edit/next/stop preservation 1, 50949 dimension
queries, clean teardown. Final tooltip-only edit rebuilt and passed
CTest 6/6; actual OBS 32.2 Fluffy Dark isolated dock rendered two
resolved rows and one fallback while 1080p playback progressed.
Screenshot saved in build/playlist-theme.JMIZXi/playlist.png (not
tracked). Remaining: durations, remaining time, shuffle/repeat;
automatic silent-stall detection, public-release checklist.
No installation, push, visibility change or production restart.

## v0.5.0 — self-contained media runtime
Official yt-dlp Linux standalone 2026.11.23 and Deno 2.9.8 pinned
by upstream release asset SHA-256 and size, both verified and executed.
scripts/prepare-runtime.py prepares the bundle from official release
assets; CMake stages the executable pair in data/runtime. OBS module
data discovery prefers the complete bundled pair, with complete system
yt-dlp + Deno/Node fallback for source-only builds. No external curl:
Qt HTTPS probes enforce the same 1 MiB response limit, cancellation,
5-second overall deadline and validated HTTPS googlevideo redirects.
CTest 7/7 including runtime precedence, executable permissions and URL
validation. Actual staged module playback smoke exited 0: 1080p video,
source/mixed audio, pause/seek/resume, visual metadata, reorder, next,
natural EOF auto-next, queue preservation, 46974 dimension queries,
teardown. Second smoke ran with a fresh HOME and empty PATH, exited 0:
bundled Deno logged, real playback/audio restored after injected child
stop at 50133 ms, second retry cancelled, queue preserved, 27404 queries
and teardown. No system extractor, Python, JS engine or curl needed at
runtime. Staged module build ID 151911b621c1616f00586ced9b2577210ee24919.
Local runtime size 131 MiB uncompressed, Linux x86_64 only. No binaries
tracked; build/bundle-stage-path records tested stage. In-dock updater
and full upstream binary redistribution compliance remain future work.
No production install or restart, push or visibility change.

## v0.5.1 — opt-in extractor updates
Dock engine menu checks stable upstream yt-dlp release on request, validates
exact asset URL, HTTPS redirect hosts, size, API SHA-256, and candidate
--version before atomic activation. Versioned executables live in the OBS
plugin configuration directory. Bundled original stays untouched; rollback
clears active marker without removing running executable. New bundle
version beats stale override. Bounded worker, cancellation at dock shutdown,
cross-process update lock; no automatic background updates. Deno pinned.
Real offscreen dock action test upgraded official 2026.10.26 to 2026.11.23,
second check reported up-to-date, rollback restored original, destruction
during check succeeded. Real staged updated-extractor playback with empty
PATH and fresh HOME: 1080p video and mixer audio, seek, pause/resume, injected
decoder interruption, fresh resolution and position restored, manual Stop
cancelled second retry, queue preserved, 28870 concurrent dimension queries,
clean teardown. CTest 8/8. Preserved exact upstream tagged license notices
under third-party/ and staged with bundle. GPLv3+ standalone yt-dlp confirmed
from upstream README; corresponding-source obligations and Deno dependency
notices still need public distribution review. No public release, production
install or push. Stage recorded in build/runtime-updater-stage-path.

## v0.5.1 — package and repository checkpoint (2026-11-24)
CPack generates a local Linux x86_64 TGZ with the correct
obs-streaming-media/{bin/64bit,data/runtime} layout and SHA-256 sidecar.
Bundled archive in build/packages/streaming-media-source-0.5.1-linux-x86_64-bundled.tar.gz
(80 MiB). Checksum, executable permissions, notices and linked library
resolution verified. Extracted archive smoke ran with empty PATH and fresh
HOME; exit 0, 1080p video/audio, pause/seek/resume, metadata and queue
reorder, natural EOF advancement, stop preservation, 43944 concurrent
dimension queries and teardown passed. CTest 8/8. Captured current v0.5.1
dock from isolated OBS 32.2/Colorway Fluffy Dark with disposable demo
scene; clean process exit, OBS reported 3 memory leaks (not attributed).
README screenshot, installation/troubleshooting, GitHub bug template
added. Common credential-pattern scan across reachable commits returned
no matches; historical machine-specific notes still require public
review. Local package only; no release assets uploaded, no production
installation, no change to repository visibility. Owner requested push
to existing private remote. Public license choice, corresponding-source
and Deno dependency obligations, and cross-distro ABI qualification remain
open. No history rewrite.

## v0.6.0 — playlist link import
Dock accepts supported static public YouTube playlist URLs. Mixed video/
playlist links offer This video or Entire playlist. Queue appends in order
without interrupting playback; Play selects first and prepends remainder,
enabling auto-advance without evicting previous queue. One flat-playlist
enumeration, at most 101 entries, 45s deadline, 4 MiB cap, cancellable,
bound to selected source UUID. Source applies validated batch on video
thread and reports actual counts through bounded receipt bookkeeping.
Titles/channels/durations cached by video ID; queue summary shows known
duration and unknown count; URLs persist, imported metadata not persisted.
Known inaccessible entries skipped; unknown availability may fail later.
Red test missing import API before implementation; CTest 9/9.
Actual bundled engine with empty PATH and fresh HOME, public Blender
uploads playlist: cancel left one original entry; Queue added 99 entries
and preserved original playback and entry, metadata/summary passed;
Play selected first, kept 100 upcoming including old entry, enabled
auto-advance, real new playback/mixer audio passed. Import smoke exit 0,
24178 dimension queries, teardown. Normal regression exit 0: 1080p,
transport, thumbnails, reorder, EOF/auto-next, queue edit/stop preservation,
50741 dimension queries and teardown. Mixed-link menu and source-change
cancellation not separately exercised in live smoke. No production install
or push. Concurrent CI documentation files outside this slice left untouched.

## Linux OBS32 CI and README consolidation (2026-11-24)

- Reuse the source-pinned SDK approach from Framerot, strict native testing
  and install-layout verification from Timeline, and product-first README
  conventions established across the OBS plugin portfolio. The initial
  Aftermarket and Streaming Media pilots share identical self-contained
  verification helpers; do not introduce a remote shared-workflow dependency
  until the hosted jobs qualify.
- Downloaded and SHA-256-verified OBS 32.2.0, built its SDK locally in an
  isolated directory, and clean-built committed source 0dc4ed0 plus the
  scoped CI changes. All 9 native CTest cases passed; no skips.
  Staged package contained only the expected native module; no embedded
  RPATH/RUNPATH or unresolved shared libraries, and both libobs libraries
  resolved to the freshly built SDK. Checksum and extracted bytes verified.
  Archive SHA-256: 8ab4f6c45a068f030f5f9fe852de6728c8a708f68575194206b479538b7c948e.
  Evidence: /tmp/obs-ci-pilots.Kk7s56/streaming-current-ci.log.
- README relative link regression, shell syntax and YAML structure passed.
  Hosted GitHub Actions runs are not yet established; no push, release,
  source export, production installation, or OBS restart. Streaming Media's
  CI deliberately excludes bundled yt-dlp/Deno and live network playback;
  release licensing and wider platform qualification remain separate gates.

## License preparation and public CI status
Repository visibility confirmed PUBLIC. Previously budget-blocked Linux
Actions run 45412791743 attempt 2 completed successfully at ff306cd.
Owner asked to determine licensing; Desktop Commander invocation taken
as permission to prepare a local GPL-3.0-or-later adoption checkpoint,
not explicit permission to publish the license. Full GPLv3 text copied
verbatim from the installed common license, README selects version 3 or
later for original project work, third-party terms preserved separately.
Checked OBS/libobs and pinned obs-websocket GPLv2-or-later headers; Qt
GPLv3 route available. docs/licensing.md records source offer, provenance,
dependency boundaries and preferred module-only distribution plus
consented verified first-run upstream executable downloads. First-run
provisioning and cross-platform selection are not implemented. Bundled
third-party source/notice obligations and exact linked dependency review
remain public binary release gates; no binary publication.
CMake installs license and guide in data/licenses. Before change,
staged installation lacked the license; after, both files match the
source. CTest 9/9. Fresh pinned OBS32 SDK verifier exit 0, 9 tests,
exact module-plus-notices allowlist, dependency and archive checksum
validation; both extracted notices match originals. README links passed.
No production install, push, or release. Await owner confirmation before
publishing the prepared license checkpoint.

## User-installed runtime setup
Owner prefers straightforward official download links/instructions rather
than requiring runtime redistribution or automatic provisioning. Added
docs/runtime-setup.md with platform-specific pinned asset links, SHA-256
values verified against the official yt-dlp 2026.11.23 and Deno v2.9.8
GitHub release metadata, and file placement/permission/refresh instructions.
Dock engine menu opens official download pages, writable runtime/manual
directory, setup guide and status refresh. Complete pinned bundle stays
first, complete manual pair next, then system fallback; incomplete pairs
are never mixed with manual files. Windows executable naming prepared but
native Windows/macOS builds are not qualified. Manual files are supplied
and checked by the user; no automatic verification or download claimed.
Manual installs require replacing the files while OBS is closed; updater
and rollback remain specific to Linux x86_64 bundled installations.
Red runtime-test missing manual selection API; green CTest 9/9 including
complete/incomplete manual pairs, precedence and empty path. Real isolated
libobs smoke used empty module data, fresh HOME, empty PATH, and official
runtime executables linked into the manual config directory. Logged
user-provided Deno runtime, 1080p video/mixer audio, transport, playlist
cancel/99-item append without interruption, first-video playback,
40608 concurrent dimension queries and teardown; exit 0. README links
passed. No production install or public push; guide URL becomes available
on master when the prepared local commits are published. License
checkpoint remains local pending owner approval.

## Twitch provider expansion — feature/twitch-media
Owner approved provider-specific validation rather than unrestricted yt-dlp
input, starting with Twitch live channels and clips; audio-only is desired
later, not part of this slice. Branch from verified master 4dba700, separate
from ci/native-platforms / PR #1. Retain streaming_media_youtube source ID
and OBS FFmpeg playback/audio ownership. New source display name is
Streaming Media. Existing YouTube playlist import and preview stay separate.
Canonical Twitch channel and clip inputs, exact extractor selection,
provider-specific CDN checks and bounded HTTPS preflight; live HLS rejects
foreign/relative segment URLs, nested masters and URI-bearing tags in the
initial response. Ongoing FFmpeg live refreshes are not host-filtered by
the Qt validator; protocol whitelist excludes file and HTTP. Not a general
network sandbox. Twitch clips use progressive MP4 with ftyp preflight and
forced MOV/MP4 demuxer; unknown codec metadata from TwitchClips is accepted
only for MP4, explicit missing tracks rejected. Three public clips observed
the exact d1ndex63qxojbr.cloudfront.net host; no generic CloudFront allowance.
Mixed-provider queue, canonical alias dedup, source/dock inputs, opt-in
request path wired; remote WebSocket integration not separately live-tested.
Twitch queue uses URL labels until playback; thumbnails/metadata prefetch,
offline waiting, VODs, audio-only, authenticated/DRM streams deferred.
RED: new provider API and real Twitch clip missing-codec tests; GREEN all
9 CTest tests. Final isolated real Linux libobs smokes with fresh HOME and
empty PATH, bundled official runtime, both exit 0:
- build/twitch-final-live.log: lofigirl live 1920x1080, nonblack16,
  source audio27, mixed26, queue/continuing/stop/preserved all 1,
  7154 dimension queries, source released.
- build/twitch-final-clip.log: public Lofi Girl clip 1080x1920, duration60012,
  nonblack28, source audio42, mixed45, pause/seek/resume all 1,
  queue/continuing/stop/preserved all 1, 21739 queries, source released.
- build/twitch-youtube-regression.log: earlier same provider slice,
  YouTube 1920x1080 audio/video and transport, public playlist cancel,
  append99, metadata, first-video autoplay; 33081 queries, exit 0.
  Last small Twitch-only UI/canonicalization and clip changes followed
  by full deterministic tests; YouTube smoke not repeated after them.
No production install, restart, merge or push. Native CI PR unchanged.
Retain this feature branch until native CI is integrated; then reconcile
and qualify Windows/macOS Twitch paths. Real live long-session and recovery
remain unverified; no claims about ad removal or broadcast rights.

## Provider metadata enrichment
Owner requested pulling as much useful information as available. Context7
yt-dlp official README/CONTRIBUTING fields consulted (/yt-dlp/yt-dlp);
verified actual pinned runtime responses for live Twitch, clip and YouTube.
Optional fields are omitted, not fabricated; no raw format JSON propagated.
Normalized metadata (bounded plain text, author vs uploader, description,
category, date, counts, duration, reported resolution list and validated
thumbnail) travels through a separate read-only source info procedure.
Current item reuses playback extraction; no duplicate extraction required.
Dock displays compact author/category with escaped tooltip details. Twitch
image host exact static-cdn.jtvnw.net, HTTPS/default port/no credentials,
no redirects, 1 MiB and bounded JPEG/PNG/WebP dimensions. Visible Twitch
queue rows use one metadata-only QProcess at a time, exact extractor,
20s timeout, 4 MiB stdout, no media download, cancelled on source change
or shutdown. Cache keyed by canonical provider URL (existing YouTube IDs
retained), session-only; current metadata also populates matching rows.
YouTube queue oEmbed path retained. Live counts/preview are snapshots,
not periodic polling. Offline/missing fields fall back to labels; failed
lookup not automatically retried until cache eviction/session change.
RED missing metadata API; GREEN 9/9 CTest. Actual isolated Linux OBS
with empty PATH and fresh HOME:
- build/info-live.log exit 0, 1920x1080, nonblack16/audio25/mixed23,
  current author/details, current thumbnail, queued clip metadata and
  thumbnail, uninterrupted audio, queue/stop/preservation all 1;
  9835 dimension queries. Screenshot build/twitch-live-info.png.
- build/info-clip.log exit 0, 1080x1920, nonblack29/audio44/mixed46,
  pause/seek/resume 1; current/thumbnail/queued live metadata/continuing
  all 1; 15864 queries. Screenshot build/twitch-clip-info.png.
- build/info-youtube-regression.log exit 0, 1920x1080,
  nonblack26/audio39/mixed40, transport all 1, playlist cancel/import99,
  no playback interruption, metadata/first play all 1, 28898 queries.
README links passed. No production install/restart or push, PR #1 unchanged.
Remaining: audio-only and Twitch VODs, richer user-requested live info if
extractor exposes it, provider metadata refresh policy, native platform
qualification. No continuous stats or extra Twitch API credentials.

## Completed public Twitch VOD support
Owner requested Twitch VODs. Added exact numeric canonical URL path,
shared twitch:vod/TwitchVod extractor selection for playback and queue
metadata, canonical duplicate detection and VOD labels. Public samples
2928653119 (MPEG-TS, 33077.044s) and 2931942986 (fragmented MP4,
33514.267s) resolved via pinned yt-dlp; both use the observed exact CDN
d2nvs31859zcd8.cloudfront.net. Bounded finite HLS snapshots normalize
relative media/init URIs against the validated final HTTPS response URL.
Reject missing ENDLIST, foreign/local/non-HTTPS segments, nested playlists,
encryption and unrecognized directives. No generic CloudFront widening.
Private snapshot only, native OBS decoder streams the media on demand.

Actual initial VOD tests revealed native OBS clock uses stream PTS
(8502.448s TS, 62.044s fMP4 actual ffprobe start_time). Native zero-relative
seek failed for both. VodClock captures first observed positive decoded
PTS per epoch, rebases elapsed time and TS native seek/recovery targets.
Sampled TS origin approximately 2s after true PTS origin; not frame-exact.
TS near-end natural EOF works despite initial/reset native seek warning.
fMP4 native seek scanned many segments and did not resume reliably.
Implemented bounded segment index/slice using EXTINF, media sequence,
initialization map and discontinuity state. fMP4 seeks re-resolve and reopen
a private playlist at the containing segment; child native seek disabled.
Full duration and segment timeline offset preserved. Manual seek requests
epoch-tagged, processed on video tick, desired paused state carried across
reopen. Recovery reuses same segment path; seek while remaining paused and
interrupted-VOD recovery not independently real-qualified. Long uninterrupted
timestamp-wrap playback not tested. Time query arguments stripped.
Unsupported encrypted/ongoing/unusual HLS VODs rejected.

RED/green parser, clock, slice and queue tests, all 9 CTest cases pass.
Real isolated Linux OBS, fresh HOME/empty PATH, pinned runtime:
- build/twitch-vod-indexed.log exit 0: public fMP4 1920x1080,
  nonblack32/source audio52/mixed51, pause/seek/resume 1, 120000ms
  segment start and time120368, near-end indexed 33500000ms start,
  natural EOF position33514233 / duration33514267, metadata/current
  thumbnail/queued VOD metadata and audio after automatic TS next all 1,
  30791 dimension queries and source teardown.
- build/twitch-vod-final-ts.log completed test: public TS 1920x1080,
  nonblack31/source audio127/mixed127, pause/seek/resume 1, time121190,
  natural EOF position33075024/duration33077044, VOD_INFO all 1,
  auto advanced to fMP4 with audio; 23176 queries and source teardown.
- build/twitch-vod-final-youtube.log exit 0: YouTube 1920x1080,
  nonblack26/source audio40/mixed41, pause/seek/resume and thumbnail 1,
  playlist cancel/import99/uninterrupted/metadata/first autoplay 1,
  23215 dimension queries and source teardown.
README/CHANGELOG/AGENTS updated; no production install or restart, no push,
cross-platform PR untouched. Remaining audio-only, offline waiting,
live refresh, cross-platform playback and broader VOD qualification.
