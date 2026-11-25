#include "media-queue.hpp"
#include "playlist-metadata.hpp"
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
  check(canonicalProviderUrl(
            "https://soundcloud.com/artist/song?utm_source=test") ==
            "https://soundcloud.com/artist/song",
        "canonical SoundCloud track");
  check(canonicalProviderUrl("https://artist.bandcamp.com/track/song") ==
            "https://artist.bandcamp.com/track/song",
        "canonical Bandcamp track");
  check(canonicalProviderUrl("https://soundcloud.com/artist/sets/album")
                .isEmpty() &&
            canonicalProviderUrl("https://artist.bandcamp.com/album/song")
                .isEmpty() &&
            canonicalProviderUrl(
                "https://artist.bandcamp.com.evil.test/track/song")
                .isEmpty(),
        "reject collections and foreign hosts in track route");
  check(providerExtractorKey("https://soundcloud.com/artist/song") ==
                "Soundcloud" &&
            providerExtractorKey("https://artist.bandcamp.com/track/song") ==
                "Bandcamp",
        "exact audio extractor identities");
  QByteArray music =
      R"({"extractor_key":"Soundcloud","title":"Track","formats":[{"url":"https://cf-media.sndcdn.com/a","protocol":"http","ext":"mp3","acodec":"mp3","vcodec":"none","abr":128},{"url":"https://evil.test/a","protocol":"https","ext":"mp3","acodec":"mp3","vcodec":"none","abr":320}]})";
  auto audio = selectAudioMedia(
      music, [](const QString &, bool) { return true; },
      MediaProvider::SoundCloud);
  check(audio.error.isEmpty() &&
            audio.direct == "https://cf-media.sndcdn.com/a" &&
            audio.manifest.isEmpty(),
        "select dedicated progressive HTTPS audio");
  check(!selectAudioMedia(
             music, [](const QString &, bool) { return true; },
             MediaProvider::Bandcamp)
             .error.isEmpty(),
        "provider isolation");
  QByteArray albumTrack =
      R"({"extractor_key":"Bandcamp","formats":[{"url":"https://t4.bcbits.com/stream","format_id":"mp3-128","protocol":"https","ext":"mp3","acodec":"mp3","vcodec":"none","abr":128},{"url":"https://popplers5.bandcamp.com/download","format_id":"flac","protocol":"https","ext":"flac","acodec":"flac","vcodec":"none"}]})";
  check(selectAudioMedia(
            albumTrack, [](const QString &, bool) { return true; },
            MediaProvider::Bandcamp)
                .direct == "https://t4.bcbits.com/stream",
        "Bandcamp public stream only");
  QByteArray ytAudio =
      R"({"extractor_key":"Youtube","formats":[{"url":"https://r.googlevideo.com/combined","protocol":"https","ext":"mp4","acodec":"mp4a.40.2","vcodec":"avc1","abr":320},{"url":"https://r.googlevideo.com/drm","protocol":"https","ext":"m4a","acodec":"mp4a.40.2","vcodec":"none","has_drm":true,"abr":320},{"url":"https://r.googlevideo.com/audio","protocol":"https","ext":"m4a","acodec":"mp4a.40.2","vcodec":"none","abr":128}]})";
  check(selectAudioMedia(
            ytAudio, [](const QString &, bool) { return true; },
            MediaProvider::YouTube)
                .direct == "https://r.googlevideo.com/audio",
        "audio only excludes video and DRM");
  const QString scHls =
      "https://playback.media-streaming.soundcloud.cloud/track.m3u8";
  QByteArray aac =
      R"({"extractor_key":"Soundcloud","formats":[{"url":"https://playback.media-streaming.soundcloud.cloud/track.m3u8","protocol":"m3u8_native","ext":"m4a","acodec":"mp4a.40.2","vcodec":"none","abr":160}]})";
  auto hlsAudio = selectAudioMedia(
      aac, [](const QString &, bool hls) { return hls; },
      MediaProvider::SoundCloud);
  check(hlsAudio.error.isEmpty() && hlsAudio.hlsDirect,
        "SoundCloud dedicated AAC HLS");
  QByteArray scManifest = "#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:10,"
                          "\npart.m4s\n#EXTINF:7,\npart2.m4s\n#EXT-X-ENDLIST\n";
  auto normalized =
      normalizeFinitePlaylist(scManifest, scHls, MediaProvider::SoundCloud);
  check(!normalized.isEmpty(), "SoundCloud fMP4 manifest");
  auto scSliced = sliceFinitePlaylist(normalized, 11000, false);
  check(scSliced.startMs == 10000 && scSliced.durationMs == 17000,
        "SoundCloud fragment indexed seek");
  auto encrypted = scManifest;
  encrypted.replace("#EXTINF:10,",
                    "#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"key\"\n#EXTINF:10,");
  check(normalizeFinitePlaylist(encrypted, scHls, MediaProvider::SoundCloud)
            .isEmpty(),
        "encrypted HLS rejected");
  auto foreign = scManifest;
  foreign.replace("part.m4s", "https://evil.test/part.m4s");
  check(normalizeFinitePlaylist(foreign, scHls, MediaProvider::SoundCloud)
            .isEmpty(),
        "foreign segment rejected");
  const QString tt =
      "https://www.tiktok.com/@creator.name/video/6742501081818877190";
  check(canonicalProviderUrl("https://m.tiktok.com/@creator.name/video/"
                             "6742501081818877190?share=1") == tt,
        "canonical TikTok public post");
  check(mediaProvider(tt) == MediaProvider::TikTok &&
            providerExtractor(tt) == "tiktok" &&
            providerExtractorKey(tt) == "TikTok" &&
            validSourceMediaUrl(tt, false) && !validSourceMediaUrl(tt, true),
        "TikTok is video only");
  for (const auto &bad : QStringList{
           "https://tiktok.com.evil.test/@creator/video/6742501081818877190",
           "https://www.tiktok.com/@creator/live",
           "https://www.tiktok.com/@creator/photo/6742501081818877190",
           "https://vm.tiktok.com/abc",
           "https://www.tiktok.com:444/@creator/video/6742501081818877190",
           "https://user@www.tiktok.com/@creator/video/6742501081818877190"})
    check(canonicalProviderUrl(bad).isEmpty(), "reject unsupported TikTok URL");
  check(validMediaUrl("https://v16-webapp-prime.us.tiktok.com/stream",
                      MediaProvider::TikTok) &&
            !validMediaUrl(
                "https://v16-webapp-prime.us.tiktok.com.evil.test/stream",
                MediaProvider::TikTok) &&
            !validMediaUrl("https://www.tiktok.com/stream",
                           MediaProvider::TikTok),
        "observed TikTok media hosts only");
  check(validThumbnailUrl("https://p16-common-sign.tiktokcdn-us.com/image",
                          MediaProvider::TikTok) &&
            !validThumbnailUrl(
                "https://p16-common-sign.tiktokcdn-us.com.evil.test/image",
                MediaProvider::TikTok),
        "TikTok artwork host");
  QByteArray portrait =
      R"({"extractor_key":"TikTok","title":"Portrait","formats":[
    {"url":"https://v16-webapp-prime.us.tiktok.com/portrait","protocol":"https","ext":"mp4","vcodec":"h264","acodec":"aac","width":576,"height":1024,"tbr":800},
    {"url":"https://v19-webapp-prime.us.tiktok.com/denied","protocol":"https","ext":"mp4","vcodec":"h264","acodec":"aac","width":576,"height":1024,"tbr":900},
    {"url":"https://v16-webapp-prime.us.tiktok.com/drm","protocol":"https","ext":"mp4","vcodec":"h264","acodec":"aac","width":576,"height":1024,"tbr":2000,"has_drm":true},
    {"url":"https://v16-webapp-prime.us.tiktok.com/hevc","protocol":"https","ext":"mp4","vcodec":"h265","acodec":"aac","width":576,"height":1024,"tbr":2500},
    {"url":"https://v16-webapp-prime.us.tiktok.com/silent","protocol":"https","ext":"mp4","vcodec":"h264","acodec":"none","width":576,"height":1024,"tbr":3000}]})";
  QStringList attempted;
  auto tiktok = selectMedia(
      portrait, 720,
      [&](const QString &url, bool hls) {
        attempted << url;
        return !hls && url.endsWith("/portrait");
      },
      MediaProvider::TikTok);
  check(tiktok.error.isEmpty() && tiktok.direct.endsWith("/portrait") &&
            attempted.size() == 2 && attempted.first().endsWith("/denied"),
        "portrait short-edge quality cap and reachable combined H264/AAC "
        "fallback");
  check(!selectMedia(
             portrait, 720, [](const QString &, bool) { return false; },
             MediaProvider::TikTok)
             .error.isEmpty(),
        "deny unreachable TikTok media");
  auto foreignRoot = portrait;
  foreignRoot.replace("\"TikTok\"", "\"Generic\"");
  check(!selectMedia(
             foreignRoot, 720, [](const QString &, bool) { return true; },
             MediaProvider::TikTok)
             .error.isEmpty(),
        "exact extractor identity");
  auto protectedRoot = portrait;
  protectedRoot.replace("\"extractor_key\":",
                        "\"has_drm\":true,\"extractor_key\":");
  check(!selectMedia(
             protectedRoot, 720, [](const QString &, bool) { return true; },
             MediaProvider::TikTok)
             .error.isEmpty(),
        "root DRM denied");
  QByteArray cachedTikTok = R"({"extractor_key":"TikTok","formats":[
    {"format_id":"h264_540p_498357-0","url":"https://v16-webapp-prime.us.tiktok.com/a","protocol":"https","ext":"mp4","vcodec":"h264","acodec":"aac","width":576,"height":576,"filesize":9687204},
    {"format_id":"h264_540p_498357-1","url":"https://v19-webapp-prime.us.tiktok.com/b","protocol":"https","ext":"mp4","vcodec":"h264","acodec":"aac","width":576,"height":576,"filesize":9687204},
    {"format_id":"h264_540p_big","url":"https://v19-webapp-prime.us.tiktok.com/c","protocol":"https","ext":"mp4","vcodec":"h264","acodec":"aac","width":576,"height":576,"filesize":67108865},
    {"format_id":"h264_540p_unknown","url":"https://v19-webapp-prime.us.tiktok.com/d","protocol":"https","ext":"mp4","vcodec":"h264","acodec":"aac","width":576,"height":576},
    {"format_id":"h264_bad+other","url":"https://v19-webapp-prime.us.tiktok.com/e","protocol":"https","ext":"mp4","vcodec":"h264","acodec":"aac","width":576,"height":576,"filesize":10}]})";
  auto candidates = tiktokDownloadCandidates(cachedTikTok, 720);
  check(candidates.size() == 2 &&
            candidates[0].formatId == "h264_540p_498357-1" &&
            candidates[0].expectedBytes == 9687204 &&
            candidates[0].downloadSelector.contains(
                "[url^=\"https://v19-webapp-prime.us.tiktok.com/\"]"),
        "bounded exact-format TikTok download candidates");
  check(tiktokDownloadCandidates(cachedTikTok, 360).isEmpty(),
        "download resolution limit");
  check(tiktokDownloadCandidates(protectedRoot, 720).isEmpty(),
        "protected post cannot use download route");
  check(validThumbnailUrl("https://p19-common-sign.tiktokcdn-us.com/image",
                          MediaProvider::TikTok),
        "second observed TikTok thumbnail host");
  MediaQueue tiktokQueue;
  check(tiktokQueue.append(tt + "?share=1") &&
            tiktokQueue.items() == QStringList{tt} &&
            mediaLabel(tt).startsWith("TikTok"),
        "TikTok canonical queue and label");
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
  check(mediaProvider("https://twitch.tv/videos/123") == MediaProvider::Twitch,
        "Twitch VOD");
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
  check(canonicalTwitchUrl("https://m.twitch.tv/videos/123?t=2h") ==
            "https://www.twitch.tv/videos/123",
        "canonical VOD");
  check(canonicalTwitchUrl("https://twitch.tv/videos/abc").isEmpty(),
        "reject malformed VOD");
  check(twitchExtractor("https://twitch.tv/videos/123") == "twitch:vod",
        "VOD extractor");
  QByteArray vod =
      R"({"extractor_key":"TwitchVod","is_live":false,"formats":[{"url":"https://d2nvs31859zcd8.cloudfront.net/v/index.m3u8","protocol":"m3u8_native","vcodec":"avc1","acodec":"mp4a","height":720}]})";
  auto vm = selectMedia(
      vod, 1080, [](const QString &, bool h) { return h; },
      MediaProvider::Twitch);
  check(vm.error.isEmpty() && !vm.live && vm.hlsDirect,
        "finite combined HLS VOD");
  const QString base = "https://d2nvs31859zcd8.cloudfront.net/v/index.m3u8";
  auto normalize = [&](QByteArray b) {
    return normalizeTwitchVodPlaylist(b, base);
  };
  QByteArray playlist = "#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:5,\n0."
                        "mp4\n#EXT-X-ENDLIST\n";
  auto snapshot = normalize(playlist);
  check(snapshot.contains("https://d2nvs31859zcd8.cloudfront.net/v/init.mp4") &&
            snapshot.contains("https://d2nvs31859zcd8.cloudfront.net/v/0.mp4"),
        "absolute init and media segments");
  check(normalize("#EXTM3U\n#EXTINF:5,\n0.ts\n#EXT-X-ENDLIST\n")
            .contains("/v/0.ts"),
        "TS VOD");
  check(normalize("#EXTM3U\n#EXTINF:5,\n0.ts\n").isEmpty(),
        "require completed VOD");
  for (auto path :
       {"file:///tmp/private.ts", "https://evil.test/0.ts", "//evil.test/0.ts",
        "http://d2nvs31859zcd8.cloudfront.net/0.ts", "nested.m3u8"}) {
    check(normalize(QByteArray("#EXTM3U\n#EXTINF:5,\n") + path +
                    "\n#EXT-X-ENDLIST\n")
              .isEmpty(),
          "reject unsafe VOD segment");
  }
  check(normalize("#EXTM3U\n#EXT-X-MAP:URI=\"file:///tmp/"
                  "private.mp4\"\n#EXTINF:5,\n0.mp4\n#EXT-X-ENDLIST\n")
            .isEmpty(),
        "reject unsafe map");
  check(normalize("#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"key\n#EXTINF:5,\n0."
                  "ts\n#EXT-X-ENDLIST\n")
            .isEmpty(),
        "reject encryption");
  auto fragmented =
      normalize("#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-MEDIA-SEQUENCE:10\n#EXT-X-"
                "MAP:URI=\"init.mp4\"\n#EXTINF:5,\n0.mp4\n#EXTINF:6,\n1.mp4\n#"
                "EXTINF:4,\n2.mp4\n#EXT-X-ENDLIST\n");
  auto sliced = sliceTwitchFmp4Playlist(fragmented, 7000);
  check(sliced.startMs == 5000 && sliced.durationMs == 15000,
        "seek to containing segment");
  check(sliced.manifest.contains("#EXT-X-MEDIA-SEQUENCE:11") &&
            sliced.manifest.contains("/v/init.mp4") &&
            !sliced.manifest.contains("/v/0.mp4") &&
            sliced.manifest.contains("/v/1.mp4"),
        "slice keeps init and remaining segments");
  auto last = sliceTwitchFmp4Playlist(fragmented, 99999);
  check(last.startMs == 11000 && last.manifest.contains("/v/2.mp4"),
        "clamp to final segment");
  check(sliceTwitchFmp4Playlist(
            normalize("#EXTM3U\n#EXTINF:5,\n0.ts\n#EXT-X-ENDLIST\n"), 0)
            .manifest.isEmpty(),
        "only fragmented MP4");
  check(mediaProvider("https://kick.com/xqc") == MediaProvider::Kick,
        "Kick live");
  check(canonicalKickUrl("https://www.kick.com/XQC/videos/"
                         "01A204BB-F2E0-7379-BDD1-A56390F10160?t=1") ==
            "https://kick.com/xqc/videos/01a204bb-f2e0-7379-bdd1-a56390f10160",
        "canonical Kick VOD");
  check(canonicalKickUrl(
            "https://kick.com/xqc/clips/clip_01M7QZ864DK0K76T3Q16X01S9B") ==
            "https://kick.com/xqc/clips/clip_01M7QZ864DK0K76T3Q16X01S9B",
        "Kick clip");
  check(canonicalKickUrl("https://kick.com.evil.test/xqc").isEmpty() &&
            canonicalKickUrl("https://kick.com/xqc/videos/no").isEmpty() &&
            canonicalKickUrl("https://kick.com/xqc/clips").isEmpty(),
        "reject invalid Kick routes");
  check(validMediaUrl("https://a.playlist.live-video.net/index.m3u8",
                      MediaProvider::Kick) &&
            !validMediaUrl(
                "https://a.playlist.live-video.net.evil.test/index.m3u8",
                MediaProvider::Kick),
        "Kick edge boundary");
  check(!validMediaUrl("https://d26yk4zpyhjeeq.cloudfront.net/index.m3u8",
                       MediaProvider::Twitch),
        "Kick CDN isolated");
  check(providerExtractor("https://kick.com/xqc") == "kick:live" &&
            providerExtractorKey("https://kick.com/xqc") == "Kick",
        "Kick extractor identity");
  auto kickClip = QByteArray(
      R"({"extractor_key":"KickClip","duration":30,"formats":[{"url":"https://clips.kick.com/clip/index.m3u8","protocol":"m3u8_native","ext":"mp4"}]})");
  check(selectMedia(
            kickClip, 1080, [](const QString &, bool h) { return h; },
            MediaProvider::Kick)
            .error.isEmpty(),
        "Kick clip unknown codec and height");
  auto kickBase = QString("https://clips.kick.com/clip/index.m3u8");
  auto bytes = QByteArray("#EXTM3U\n#EXTINF:5,\n#EXT-X-BYTERANGE:100@0\nchunk."
                          "ts\n#EXT-X-ENDLIST\n");
  check(normalizeFinitePlaylist(bytes, kickBase, MediaProvider::Kick)
            .contains(
                "#EXT-X-BYTERANGE:100@0\nhttps://clips.kick.com/clip/chunk.ts"),
        "Kick explicit byte range");
  auto badRange = bytes;
  badRange.replace("100@0", "0@0");
  check(normalizeFinitePlaylist(badRange, kickBase, MediaProvider::Kick)
            .isEmpty(),
        "invalid range");
  check(
      normalizeFinitePlaylist(bytes, kickBase, MediaProvider::Twitch).isEmpty(),
      "provider isolation");
  check(validLivePlaylist(
            "#EXTM3U\n#EXTINF:2,\nhttps://a.hls.live-video.net/"
            "chunk.ts\n#EXT-X-PREFETCH:https://a.hls.live-video.net/next.ts\n",
            MediaProvider::Kick),
        "Kick live prefetch");
  check(!validLivePlaylist(
            "#EXTM3U\nhttps://a.hls.live-video.net/"
            "chunk.ts\n#EXT-X-PREFETCH:https://evil.test/next.ts\n",
            MediaProvider::Kick),
        "unsafe prefetch");
  auto kickTs = normalizeFinitePlaylist(
      "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:8\n#EXTINF:10,\n0.ts\n#EXTINF:10,\n1."
      "ts\n#EXTINF:5,\n2.ts\n#EXT-X-ENDLIST\n",
      "https://stream.kick.com/test/index.m3u8", MediaProvider::Kick);
  auto kickSlice = sliceFinitePlaylist(kickTs, 22000, true);
  check(kickSlice.startMs == 20000 && kickSlice.durationMs == 25000 &&
            kickSlice.manifest.contains("#EXT-X-MEDIA-SEQUENCE:10") &&
            !kickSlice.manifest.contains("/0.ts") &&
            kickSlice.manifest.contains("/2.ts"),
        "indexed Kick TS tail");
  check(sliceFinitePlaylist(
            normalizeFinitePlaylist(bytes, kickBase, MediaProvider::Kick), 0,
            true)
            .manifest.isEmpty(),
        "never discard clip byte range");
}
