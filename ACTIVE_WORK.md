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
