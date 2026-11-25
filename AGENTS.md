# Streaming Media
Standalone OBS C++/Qt Core plugin. Keep provider-specific validation, starting with YouTube and public Twitch/Kick
channels/clips/completed VODs, and use the existing OBS FFmpeg source as playback owner.
Do not widen supported hosts or generic extractor access without evidence. No unrelated Aftermarket edits.
Read ACTIVE_WORK.md before continuing. Build and run deterministic CTest,
then use the isolated libobs smoke for real network playback changes.
Never log signed URLs in plugin messages or persist them in scene settings.
Do not restart the user's OBS process or publish without explicit intent.

Streaming Audio shares the decoder/queue, but only accepts dedicated audio renditions for public YouTube videos and SoundCloud/Bandcamp tracks. Never silently substitute video, gated downloads or Spotify audio.
