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
}
