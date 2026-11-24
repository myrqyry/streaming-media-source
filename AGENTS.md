# Streaming Media
Standalone OBS C++/Qt Core plugin. Keep YouTube-first scope and use the
existing OBS FFmpeg source as playback owner. No unrelated Aftermarket edits.
Read ACTIVE_WORK.md before continuing. Build and run deterministic CTest,
then use the isolated libobs smoke for real network playback changes.
Never log signed URLs in plugin messages or persist them in scene settings.
Do not restart the user's OBS process or publish without explicit intent.
