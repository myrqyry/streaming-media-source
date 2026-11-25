#include "resolver.hpp"
#include <QCoreApplication>
#include <QDebug>
#include <cstdlib>
static void check(bool good, const char *message) {
  if (!good) {
    qCritical() << message;
    std::exit(1);
  }
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  check(validYoutube("https://www.youtube.com/watch?v=aqz-KE-bpKQ"),
        "watch URL");
  check(validYoutube("https://youtu.be/aqz-KE-bpKQ"), "short URL");
  check(!validYoutube("https://youtube.com.evil.test/watch?v=x"),
        "host confusion");
  check(!validYoutube("https://youtube.com@evil.test/watch?v=x"), "user info");
  check(!validYoutube("file:///tmp/foo"), "non-network input");
  QByteArray good =
      R"({"requested_formats":[{"url":"https://manifest.googlevideo.com/v","protocol":"m3u8_native","vcodec":"avc1.64002A","acodec":"none","width":1920,"height":1080,"tbr":5000},{"url":"https://manifest.googlevideo.com/a","protocol":"m3u8_native","acodec":"mp4a.40.2","vcodec":"none"}],"title":"Test","is_live":false})";
  auto r = parseMedia(good);
  check(r.error.isEmpty() && r.manifest.contains("RESOLUTION=1920x1080") &&
            r.manifest.contains("AUDIO=\"audio\"") &&
            r.manifest.contains("URI=\"https://manifest.googlevideo.com/a\""),
        "adaptive master includes both tracks");
  check(!r.live, "VOD");
  auto escaped = good;
  escaped.replace("https://manifest.googlevideo.com/a",
                  "https://manifest.googlevideo.com/a%2F%3D%4A%25");
  auto signedMedia = parseMedia(escaped);
  check(signedMedia.manifest.contains(
            "URI=\"https://manifest.googlevideo.com/a%2F%3D%4A%25\""),
        "preserve percent-encoded signed audio URL verbatim");
  auto bad = good;
  bad.replace("https://manifest.googlevideo.com/a", "file:///etc/passwd");
  check(!parseMedia(bad).error.isEmpty(), "reject unsafe media URL");
  auto single = parseMedia(
      R"({"url":"https://rr1.googlevideo.com/v","protocol":"https","vcodec":"avc1","acodec":"mp4a","title":"Direct"})");
  check(single.error.isEmpty() && single.direct.startsWith("https://") &&
            single.manifest.isEmpty(),
        "progressive fallback");
  check(
      !parseMedia(
           R"({"url":"https://rr1.googlevideo.com/v","vcodec":"avc1","acodec":"none"})")
           .error.isEmpty(),
      "reject silent fallback");
  check(!parseMedia("invalid").error.isEmpty(), "invalid JSON");
  QByteArray formats = R"({"title":"test","formats":[
 {"url":"https://manifest.googlevideo.com/a-good","protocol":"m3u8_native","vcodec":"none","acodec":null,"abr":64},
 {"url":"https://manifest.googlevideo.com/a-bad","protocol":"m3u8_native","vcodec":"none","acodec":null,"abr":128},
 {"url":"https://manifest.googlevideo.com/v-good","protocol":"m3u8_native","vcodec":"avc1","acodec":"none","width":1920,"height":1080},
 {"url":"https://manifest.googlevideo.com/v-too-big","protocol":"m3u8_native","vcodec":"avc1","acodec":"none","width":3840,"height":2160},
 {"url":"https://rr1.googlevideo.com/progressive","protocol":"https","vcodec":"avc1","acodec":"mp4a","height":360}]})";
  auto selected = selectMedia(formats, 1080, [](const QString &url, bool) {
    return !url.endsWith("a-bad");
  });
  check(selected.error.isEmpty() && selected.manifest.contains("a-good") &&
            !selected.manifest.contains("v-too-big"),
        "reject unavailable audio and honor maximum resolution");
  auto fallback = selectMedia(formats, 1080,
                              [](const QString &, bool hls) { return !hls; });
  check(fallback.error.isEmpty() && fallback.direct.endsWith("progressive"),
        "fallback only to validated combined media");
  check(!selectMedia(formats, 1080, [](const QString &, bool) { return false; })
             .error.isEmpty(),
        "no silent playback if every rendition fails");
  check(validVodPlaylist("#EXTM3U\n#EXTINF:5,\nhttps://rr1.googlevideo.com/"
                         "segment\n#EXT-X-ENDLIST\n"),
        "validated VOD snapshot");
  check(!validVodPlaylist(
            "#EXTM3U\n#EXTINF:5,\nfile:///etc/passwd\n#EXT-X-ENDLIST\n"),
        "unsafe nested URI");
  check(!validVodPlaylist(
            "#EXTM3U\n#EXTINF:5,\nhttps://rr1.googlevideo.com/segment\n"),
        "live playlist cannot be snapshotted");
  check(!validVodPlaylist(
            "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"file:///etc/"
            "passwd\"\nhttps://rr1.googlevideo.com/segment\n#EXT-X-ENDLIST\n"),
        "reject unvalidated key URI");
  check(mediaProvider("https://www.twitch.tv/LofiGirl") ==
            MediaProvider::Twitch,
        "Twitch channel");
  check(mediaProvider("https://clips.twitch.tv/ExampleClip-123") ==
            MediaProvider::Twitch,
        "Twitch clip");
  check(mediaProvider("https://twitch.tv/user/clip/ExampleClip-123") ==
            MediaProvider::Twitch,
        "channel clip");
  check(mediaProvider("https://twitch.tv.evil.test/user") ==
            MediaProvider::Unknown,
        "Twitch host spoof");
  check(mediaProvider("https://twitch.tv/videos/123") == MediaProvider::Unknown,
        "VOD deferred");
  check(mediaProvider("https://twitch.tv/directory") == MediaProvider::Unknown,
        "not a channel");
  check(!validMediaUrl("https://foo.ttvnw.net/video", MediaProvider::YouTube),
        "provider isolation");
  check(validMediaUrl("https://foo.ttvnw.net/video", MediaProvider::Twitch),
        "Twitch CDN");
  check(!validMediaUrl("https://foo.ttvnw.net.evil.test/video",
                       MediaProvider::Twitch),
        "CDN spoof");
  check(!validMediaUrl("https://rr1.googlevideo.com/video",
                       MediaProvider::Twitch),
        "reject cross-provider");
  QByteArray twitch =
      R"({"extractor_key":"TwitchStream","is_live":true,"formats":[{"url":"https://use22.playlist.ttvnw.net/hls","protocol":"m3u8_native","vcodec":"avc1","acodec":"mp4a","height":1080},{"url":"https://use22.playlist.ttvnw.net/audio","protocol":"m3u8_native","vcodec":"none","acodec":"mp4a"}]})";
  auto stream = selectMedia(
      twitch, 1080, [](const QString &, bool) { return true; },
      MediaProvider::Twitch);
  check(stream.error.isEmpty() && stream.live && stream.direct.endsWith("/hls"),
        "combined Twitch live stream");
  check(!selectMedia(
             twitch, 720, [](const QString &, bool) { return true; },
             MediaProvider::Twitch)
             .error.isEmpty(),
        "quality cap and audio-only deferred");
  QByteArray clip =
      R"({"extractor_key":"TwitchClips","formats":[{"url":"https://clips-media-assets2.twitch.tv/clip.mp4","protocol":"https","vcodec":"avc1","acodec":"mp4a","height":720}]})";
  check(selectMedia(
            clip, 1080, [](const QString &, bool) { return true; },
            MediaProvider::Twitch)
            .error.isEmpty(),
        "Twitch clip");
  check(!selectMedia(clip, 1080, [](const QString &, bool) { return true; })
             .error.isEmpty(),
        "Twitch response rejected by YouTube");
  QByteArray realClipShape =
      R"({"extractor_key":"TwitchClips","formats":[{"url":"https://d1ndex63qxojbr.cloudfront.net/clip.mp4","protocol":"https","ext":"mp4","vcodec":null,"acodec":null,"height":720}]})";
  check(selectMedia(
            realClipShape, 1080, [](const QString &, bool) { return true; },
            MediaProvider::Twitch)
            .error.isEmpty(),
        "real clip schema with unknown codecs");
  auto silent = realClipShape;
  silent.replace("\"acodec\":null", "\"acodec\":\"none\"");
  check(!selectMedia(
             silent, 1080, [](const QString &, bool) { return true; },
             MediaProvider::Twitch)
             .error.isEmpty(),
        "reject explicitly silent clip");
  auto unknownCdn = realClipShape;
  unknownCdn.replace("d1ndex63qxojbr", "unrelated");
  check(!selectMedia(
             unknownCdn, 1080, [](const QString &, bool) { return true; },
             MediaProvider::Twitch)
             .error.isEmpty(),
        "no generic CloudFront allowance");
  check(validTwitchLivePlaylist(
            "#EXTM3U\n#EXTINF:2,\nhttps://foo.ttvnw.net/seg.ts\n"),
        "live CDN playlist");
  check(!validTwitchLivePlaylist("#EXTM3U\n#EXTINF:2,\nfile:///tmp/secret\n"),
        "local segment rejected");
  check(!validTwitchLivePlaylist(
            "#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://evil.test/"
            "key\"\nhttps://foo.ttvnw.net/seg.ts\n"),
        "key URI rejected");
}
