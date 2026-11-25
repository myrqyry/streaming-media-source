#include "resolver.hpp"
#include "playlist-metadata.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <algorithm>
#include <cmath>
bool validYoutube(const QString &s) {
  QUrl u(s, QUrl::StrictMode);
  auto h = u.host().toLower();
  return u.isValid() && u.scheme() == "https" && u.userInfo().isEmpty() &&
         (u.port() == -1 || u.port() == 443) &&
         (h == "youtube.com" || h == "www.youtube.com" ||
          h == "m.youtube.com" || h == "youtu.be");
}
static bool httpsUrl(const QString &s, const QUrl &u) {
  return !s.isEmpty() && u.isValid() && u.scheme() == "https" &&
         !u.host().isEmpty() && u.userInfo().isEmpty() &&
         (u.port() == -1 || u.port() == 443) && !s.contains('"') &&
         !s.contains('\n') && !s.contains('\r') && !s.contains('\\');
}
QString canonicalTwitchUrl(const QString &s) {
  QUrl u(s, QUrl::StrictMode);
  if (s.size() > 2048 || !httpsUrl(s, u))
    return {};
  auto host = u.host().toLower();
  auto path = u.path(QUrl::FullyEncoded);
  static const QRegularExpression channel("^/([A-Za-z0-9_]{1,25})/?$");
  static const QRegularExpression clip("^/([A-Za-z0-9_-]{1,200})/?$");
  static const QRegularExpression channelClip(
      "^/[A-Za-z0-9_]{1,25}/clip/([A-Za-z0-9_-]{1,200})/?$");
  if (host == "clips.twitch.tv") {
    auto m = clip.match(path);
    return m.hasMatch() ? "https://clips.twitch.tv/" + m.captured(1)
                        : QString{};
  }
  if (host != "twitch.tv" && host != "www.twitch.tv" && host != "m.twitch.tv")
    return {};
  static const QRegularExpression vod("^/videos/([1-9][0-9]{0,19})/?$");
  auto v = vod.match(path);
  if (v.hasMatch())
    return "https://www.twitch.tv/videos/" + v.captured(1);
  auto c = channelClip.match(path);
  if (c.hasMatch())
    return "https://clips.twitch.tv/" + c.captured(1);
  auto m = channel.match(path);
  if (!m.hasMatch())
    return {};
  auto id = m.captured(1).toLower();
  static const QStringList reserved{
      "directory", "downloads", "settings",      "videos",    "clip",
      "clips",     "search",    "subscriptions", "inventory", "wallet",
      "login",     "signup",    "about",         "jobs",      "p",
      "turbo",     "drops",     "prime",         "friends",   "messages",
      "dashboard", "moderator", "team",          "store",     "products"};
  return reserved.contains(id) ? QString{} : "https://www.twitch.tv/" + id;
}
QString twitchExtractor(const QString &url) {
  auto u = canonicalTwitchUrl(url);
  if (u.isEmpty())
    return {};
  if (u.startsWith("https://clips.twitch.tv/"))
    return "twitch:clips";
  if (u.startsWith("https://www.twitch.tv/videos/"))
    return "twitch:vod";
  return "twitch:stream";
}
QString twitchExtractorKey(const QString &url) {
  auto e = twitchExtractor(url);
  return e == "twitch:clips"    ? "TwitchClips"
         : e == "twitch:vod"    ? "TwitchVod"
         : e == "twitch:stream" ? "TwitchStream"
                                : QString{};
}
QString canonicalKickUrl(const QString &s) {
  QUrl u(s, QUrl::StrictMode);
  if (s.size() > 2048 || !httpsUrl(s, u) ||
      (u.host() != "kick.com" && u.host() != "www.kick.com"))
    return {};
  auto path = u.path(QUrl::FullyEncoded);
  static const QRegularExpression channel("^/([A-Za-z0-9_-]{1,40})/?$");
  static const QRegularExpression vod(
      "^/([A-Za-z0-9_-]{1,40})/videos/"
      "([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]"
      "{12})/?$");
  static const QRegularExpression clip(
      "^/([A-Za-z0-9_-]{1,40})/clips/(clip_[A-Za-z0-9]{26})/?$");
  auto v = vod.match(path);
  if (v.hasMatch())
    return "https://kick.com/" + v.captured(1).toLower() + "/videos/" +
           v.captured(2).toLower();
  auto c = clip.match(path);
  if (c.hasMatch())
    return "https://kick.com/" + c.captured(1).toLower() + "/clips/" +
           c.captured(2);
  auto m = channel.match(path);
  if (!m.hasMatch())
    return {};
  auto id = m.captured(1).toLower();
  static const QStringList reserved{
      "categories", "category",      "browse", "following", "search",
      "settings",   "login",         "signup", "dashboard", "clips",
      "videos",     "about",         "terms",  "privacy",   "help",
      "community",  "subscriptions", "api",    "embed"};
  return reserved.contains(id) ? QString{} : "https://kick.com/" + id;
}
static QString canonicalAudioTrack(const QString &s) {
  QUrl u(s, QUrl::StrictMode);
  if (s.size() > 2048 || !httpsUrl(s, u))
    return {};
  auto host = u.host().toLower(), path = u.path(QUrl::FullyEncoded);
  static const QRegularExpression sc(
      "^/([A-Za-z0-9_-]{1,100})/([A-Za-z0-9_-]{1,200})/?$");
  static const QRegularExpression bc(
      "^([a-z0-9][a-z0-9-]{0,62})\\.bandcamp\\.com$");
  static const QRegularExpression track("^/track/([A-Za-z0-9_-]{1,200})/?$");
  if (host == "soundcloud.com" || host == "www.soundcloud.com" ||
      host == "m.soundcloud.com") {
    auto m = sc.match(path);
    if (!m.hasMatch() ||
        QStringList{"discover", "search", "you", "upload", "settings", "pages",
                    "charts"}
            .contains(m.captured(1).toLower()) ||
        QStringList{"sets", "tracks", "albums", "reposts", "likes", "followers",
                    "following"}
            .contains(m.captured(2).toLower()))
      return {};
    return "https://soundcloud.com/" + m.captured(1) + "/" + m.captured(2);
  }
  if (bc.match(host).hasMatch() && host != "www.bandcamp.com") {
    auto m = track.match(path);
    if (m.hasMatch())
      return "https://" + host + "/track/" + m.captured(1);
  }
  return {};
}
bool audioOnlyProvider(MediaProvider p) {
  return p == MediaProvider::SoundCloud || p == MediaProvider::Bandcamp;
}
QString canonicalProviderUrl(const QString &url) {
  auto twitch = canonicalTwitchUrl(url);
  if (!twitch.isEmpty())
    return twitch;
  auto kick = canonicalKickUrl(url);
  return kick.isEmpty() ? canonicalAudioTrack(url) : kick;
}
QString providerExtractor(const QString &url) {
  auto audio = canonicalAudioTrack(url);
  if (!audio.isEmpty())
    return audio.startsWith("https://soundcloud.com/") ? "soundcloud"
                                                       : "bandcamp";
  auto kick = canonicalKickUrl(url);
  if (kick.isEmpty())
    return twitchExtractor(url);
  return kick.contains("/videos/")  ? "kick:vod"
         : kick.contains("/clips/") ? "kick:clip"
                                    : "kick:live";
}
QString providerExtractorKey(const QString &url) {
  auto e = providerExtractor(url);
  if (e == "soundcloud")
    return "Soundcloud";
  if (e == "bandcamp")
    return "Bandcamp";
  return e == "kick:live"   ? "Kick"
         : e == "kick:vod"  ? "KickVOD"
         : e == "kick:clip" ? "KickClip"
                            : twitchExtractorKey(url);
}
MediaProvider mediaProvider(const QString &s) {
  if (validYoutube(s))
    return MediaProvider::YouTube;
  if (!canonicalTwitchUrl(s).isEmpty())
    return MediaProvider::Twitch;
  if (!canonicalKickUrl(s).isEmpty())
    return MediaProvider::Kick;
  auto audio = canonicalAudioTrack(s);
  if (!audio.isEmpty())
    return audio.startsWith("https://soundcloud.com/")
               ? MediaProvider::SoundCloud
               : MediaProvider::Bandcamp;
  return MediaProvider::Unknown;
}
bool validMediaUrl(const QString &s, MediaProvider provider) {
  QUrl u(s, QUrl::StrictMode);
  if (!httpsUrl(s, u))
    return false;
  auto h = u.host().toLower();
  auto within = [&](const QString &domain) {
    return h == domain || h.endsWith("." + domain);
  };
  if (provider == MediaProvider::YouTube)
    return within("googlevideo.com");
  if (provider == MediaProvider::SoundCloud)
    return h == "cf-media.sndcdn.com" ||
           h == "playback.media-streaming.soundcloud.cloud";
  if (provider == MediaProvider::Bandcamp)
    return h == "t4.bcbits.com";
  if (provider == MediaProvider::Twitch) {
    // Twitch HLS edges and clip media, not arbitrary CloudFront or twitch.tv
    // hosts.
    return h.endsWith(".ttvnw.net") || h.endsWith(".twitchcdn.net") ||
           h == "clips-media-assets2.twitch.tv" ||
           h == "clips-media-assets.twitch.tv" ||
           h == "d1ndex63qxojbr.cloudfront.net" ||
           h == "d2nvs31859zcd8.cloudfront.net";
  }
  if (provider == MediaProvider::Kick)
    return h.endsWith(".live-video.net") || h == "stream.kick.com" ||
           h == "clips.kick.com" || h == "d26yk4zpyhjeeq.cloudfront.net";
  return false;
}
bool validTwitchLivePlaylist(const QByteArray &body) {
  return validLivePlaylist(body, MediaProvider::Twitch);
}
bool validLivePlaylist(const QByteArray &body, MediaProvider provider) {
  if (!body.startsWith("#EXTM3U") || body.size() > 1048576)
    return false;
  bool segment = false;
  for (auto line : body.split('\n')) {
    line = line.trimmed();
    if (line.isEmpty())
      continue;
    if (line.startsWith('#')) {
      if (line.startsWith("#EXT-X-PREFETCH:") &&
          provider == MediaProvider::Kick) {
        if (!validMediaUrl(QString::fromUtf8(line.mid(16)), provider))
          return false;
        continue;
      }
      if (line.contains("URI=") || line.startsWith("#EXT-X-STREAM-INF") ||
          line.startsWith("#EXT-X-I-FRAME-STREAM-INF") ||
          line.startsWith("#EXT-X-ENDLIST"))
        return false;
    } else {
      if (!validMediaUrl(QString::fromUtf8(line), provider))
        return false;
      segment = true;
    }
  }
  return segment;
}
QByteArray normalizeTwitchVodPlaylist(const QByteArray &body,
                                      const QString &base) {
  return normalizeFinitePlaylist(body, base, MediaProvider::Twitch);
}
QByteArray normalizeFinitePlaylist(const QByteArray &body, const QString &base,
                                   MediaProvider provider) {
  if (body.size() > 1048576 || !validMediaUrl(base, provider))
    return {};
  auto lines = body.split('\n');
  if (lines.isEmpty() || lines.first().trimmed() != "#EXTM3U")
    return {};
  QUrl parent(base, QUrl::StrictMode);
  auto resolve = [&](QByteArray raw, bool init) -> QByteArray {
    if (raw.contains('\\') || raw.contains('"') || raw.contains('\r') ||
        raw.contains('\n'))
      return {};
    QUrl part(QString::fromUtf8(raw), QUrl::StrictMode);
    if (!part.isValid() || part.hasFragment() || raw.startsWith("//"))
      return {};
    auto u = parent.resolved(part);
    auto absolute = QString::fromLatin1(u.toEncoded());
    if (!validMediaUrl(absolute, provider))
      return {};
    auto path = u.path().toLower();
    if (!(path.endsWith(".mp4") || (!init && path.endsWith(".ts")) ||
          (provider == MediaProvider::SoundCloud && !init &&
           path.endsWith(".m4s"))))
      return {};
    return absolute.toUtf8();
  };
  QByteArray result;
  bool end = false, segment = false;
  static const QRegularExpression map("^#EXT-X-MAP:URI=\"([^\"]+)\"$");
  // Whitelist only the non-URI HLS tags used by finite Twitch media
  // playlists; do not copy arbitrary extractor/server supplied directives.
  const QList<QByteArray> tags{"#EXTM3U",
                               "#EXT-X-VERSION",
                               "#EXT-X-TARGETDURATION",
                               "#EXT-X-MEDIA-SEQUENCE",
                               "#EXT-X-PLAYLIST-TYPE",
                               "#EXT-X-PROGRAM-DATE-TIME",
                               "#EXT-X-DISCONTINUITY",
                               "#EXT-X-DISCONTINUITY-SEQUENCE",
                               "#EXTINF",
                               "#EXT-X-TWITCH-ELAPSED-SECS",
                               "#EXT-X-TWITCH-TOTAL-SECS",
                               "#ID3-EQUIV-TDTG"};
  for (auto line : lines) {
    line = line.trimmed();
    if (line.isEmpty())
      continue;
    if (end)
      return {};
    if (line == "#EXT-X-ENDLIST")
      end = true;
    else if (line.startsWith("#EXT-X-MAP:")) {
      auto m = map.match(QString::fromUtf8(line));
      if (!m.hasMatch())
        return {};
      auto uri = resolve(m.captured(1).toUtf8(), true);
      if (uri.isEmpty())
        return {};
      line = "#EXT-X-MAP:URI=\"" + uri + "\"";
    } else if (line.startsWith("#EXT-X-BYTERANGE:") &&
               provider == MediaProvider::Kick) {
      static const QRegularExpression range(
          "^#EXT-X-BYTERANGE:([1-9][0-9]{0,9})@([0-9]{1,13})$");
      auto m = range.match(QString::fromUtf8(line));
      if (!m.hasMatch() || m.captured(1).toLongLong() > 1000000000LL)
        return {};
    } else if (line.startsWith('#')) {
      auto tag = line.split(':').first();
      if (!tags.contains(tag) || line.contains("URI="))
        return {};
    } else {
      line = resolve(line, false);
      if (line.isEmpty())
        return {};
      segment = true;
    }
    result += line + '\n';
    if (result.size() > 4 * 1024 * 1024)
      return {};
  }
  return end && segment ? result : QByteArray{};
}
TwitchVodSlice sliceTwitchFmp4Playlist(const QByteArray &normalized,
                                       qint64 targetMs) {
  return sliceFinitePlaylist(normalized, targetMs, false);
}
TwitchVodSlice sliceFinitePlaylist(const QByteArray &normalized,
                                   qint64 targetMs, bool allowTs) {
  // Input is our already validated finite, absolute-URI playlist. Rebuild
  // a minimal media playlist starting at the containing segment, retaining
  // the initialization map and discontinuity state of each remaining segment.
  struct Segment {
    QByteArray info, uri, map;
    qint64 start, ms;
    bool discontinuity;
  };
  QList<Segment> segments;
  QByteArray map, info;
  qint64 sequence = 0, discontinuitySequence = 0, total = 0, maxMs = 0, ms = 0;
  bool discontinuity = false, ended = false;
  for (auto line : normalized.split('\n')) {
    if (line.isEmpty())
      continue;
    if (line.startsWith("#EXT-X-MEDIA-SEQUENCE:") ||
        line.startsWith("#EXT-X-DISCONTINUITY-SEQUENCE:")) {
      bool ok = false;
      auto n = line.mid(line.indexOf(':') + 1).toLongLong(&ok);
      if (!ok || n < 0 || n > 1000000000)
        return {};
      if (line.startsWith("#EXT-X-MEDIA-SEQUENCE:"))
        sequence = n;
      else
        discontinuitySequence = n;
    } else if (line.startsWith("#EXT-X-BYTERANGE:"))
      return {};
    else if (line.startsWith("#EXT-X-MAP:"))
      map = line;
    else if (line == "#EXT-X-DISCONTINUITY")
      discontinuity = true;
    else if (line.startsWith("#EXTINF:")) {
      if (!info.isEmpty())
        return {};
      bool ok = false;
      auto sec = line.mid(8).split(',').first().toDouble(&ok);
      if (!ok || !std::isfinite(sec) || sec <= 0 || sec > 3600)
        return {};
      ms = std::llround(sec * 1000);
      if (ms <= 0)
        return {};
      info = line;
    } else if (line == "#EXT-X-ENDLIST")
      ended = true;
    else if (!line.startsWith('#')) {
      auto path = QUrl(QString::fromUtf8(line)).path().toLower();
      bool mp4 =
          (path.endsWith(".mp4") || path.endsWith(".m4s")) && !map.isEmpty();
      bool ts = allowTs && path.endsWith(".ts") && map.isEmpty();
      if (ended || info.isEmpty() || (!mp4 && !ts) ||
          total + ms > 7LL * 24 * 3600 * 1000)
        return {};
      segments.append({info, line, map, total, ms, discontinuity});
      total += ms;
      maxMs = std::max(maxMs, ms);
      info.clear();
      discontinuity = false;
    }
  }
  if (!ended || segments.isEmpty() || !info.isEmpty())
    return {};
  auto target = std::clamp<qint64>(targetMs, 0, total - 1);
  int first = 0;
  while (first + 1 < segments.size() && segments[first + 1].start <= target) {
    if (segments[first].discontinuity)
      ++discontinuitySequence;
    ++first;
  }
  QByteArray output =
      "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-"
      "TARGETDURATION:" +
      QByteArray::number((maxMs + 999) / 1000) +
      "\n#EXT-X-MEDIA-SEQUENCE:" + QByteArray::number(sequence + first) +
      "\n#EXT-X-DISCONTINUITY-SEQUENCE:" +
      QByteArray::number(discontinuitySequence) + "\n";
  QByteArray lastMap;
  for (int i = first; i < segments.size(); ++i) {
    auto &seg = segments[i];
    if (seg.discontinuity)
      output += "#EXT-X-DISCONTINUITY\n";
    if (seg.map != lastMap) {
      output += seg.map + '\n';
      lastMap = seg.map;
    }
    output += seg.info + '\n' + seg.uri + '\n';
  }
  output += "#EXT-X-ENDLIST\n";
  if (output.size() > 4 * 1024 * 1024)
    return {};
  return {output, segments[first].start, total};
}
static bool hls(const QJsonObject &o) {
  return o.value("protocol").toString().startsWith("m3u8");
}
static bool hasTrack(const QJsonObject &o, const char *key) {
  auto s = o.value(key).toString();
  return !s.isEmpty() && s != "none";
}
// Twitch clips are progressive MP4s. yt-dlp reports null/missing codec
// names for some real clips; never reinterpret explicit "none" as unknown.
static bool twitchClipFormat(const QJsonObject &root, const QJsonObject &f,
                             MediaProvider provider) {
  return provider == MediaProvider::Twitch &&
         root.value("extractor_key").toString() == "TwitchClips" &&
         !root.value("is_live").toBool() &&
         f.value("protocol").toString() == "https" &&
         f.value("ext").toString() == "mp4" && f.value("height").toInt() > 0 &&
         f.value("vcodec").toString() != "none" &&
         f.value("acodec").toString() != "none";
}
static bool kickClipFormat(const QJsonObject &root, const QJsonObject &f,
                           MediaProvider provider) {
  return provider == MediaProvider::Kick &&
         root.value("extractor_key").toString() == "KickClip" &&
         !root.value("is_live").toBool() && hls(f) &&
         f.value("ext").toString() == "mp4" &&
         f.value("vcodec").toString() != "none" &&
         f.value("acodec").toString() != "none";
}
Media parseMedia(const QByteArray &bytes, MediaProvider provider) {
  Media m;
  QJsonParseError e;
  auto j = QJsonDocument::fromJson(bytes, &e);
  if (e.error != QJsonParseError::NoError || !j.isObject()) {
    m.error = "Invalid extractor response";
    return m;
  }
  auto o = j.object();
  m.info = normalizedMediaInfo(o, provider);
  m.title = m.info.value("title").toString();
  m.live = o.value("is_live").toBool();
  auto tracks = o.value("requested_formats").toArray();
  if (provider == MediaProvider::Twitch) {
    auto key = o.value("extractor_key").toString();
    if ((key != "TwitchStream" || !m.live) &&
        (key != "TwitchClips" || m.live) && (key != "TwitchVod" || m.live)) {
      m.error = "Unsupported Twitch extractor response";
      return m;
    }
    if (tracks.size() > 1) {
      m.error = "Twitch requires a combined audio/video stream";
      return m;
    }
  }
  if (provider == MediaProvider::Kick) {
    auto key = o.value("extractor_key").toString();
    if (tracks.size() > 1 ||
        ((key != "Kick" || !m.live) && (key != "KickVOD" || m.live) &&
         (key != "KickClip" || m.live))) {
      m.error = "Unsupported Kick extractor response";
      return m;
    }
  }
  if (tracks.size() == 2) {
    QJsonObject v, a;
    for (auto t : tracks) {
      auto f = t.toObject();
      if (hasTrack(f, "vcodec") && !hasTrack(f, "acodec"))
        v = f;
      if (!hasTrack(f, "vcodec") && hasTrack(f, "acodec"))
        a = f;
    }
    auto vu = v.value("url").toString(), au = a.value("url").toString();
    // Some yt-dlp HLS audio renditions report unknown codec; the audio role is
    // explicit.
    if (a.isEmpty())
      for (auto t : tracks) {
        auto f = t.toObject();
        if (f.value("vcodec").toString() == "none" &&
            f.value("acodec").toString() != "none" &&
            f.value("url").toString() != vu)
          a = f;
      }
    au = a.value("url").toString();
    int w = v.value("width").toInt(), h = v.value("height").toInt();
    if (!hls(v) || !hls(a) || !validMediaUrl(vu, provider) ||
        !validMediaUrl(au, provider) || w <= 0 || h <= 0 || w > 7680 ||
        h > 4320) {
      m.error = "Unsupported or unsafe adaptive streams";
      return m;
    }
    int bw = std::clamp(int(v.value("tbr").toDouble(8000) * 1000), 100000,
                        100000000);
    m.videoUrl = vu;
    m.audioUrl = au;
    m.manifest = QString("#EXTM3U\n#EXT-X-VERSION:4\n#EXT-X-MEDIA:TYPE=AUDIO,"
                         "GROUP-ID=\"audio\",NAME=\"Audio\",DEFAULT=YES,"
                         "AUTOSELECT=YES,URI=\"%1\"\n#EXT-X-STREAM-INF:"
                         "BANDWIDTH=%2,RESOLUTION=%3x%4,AUDIO=\"audio\"\n%5\n")
                     .arg(au, QString::number(bw), QString::number(w),
                          QString::number(h), vu);
    return m;
  }
  auto f = tracks.size() == 1 ? tracks.at(0).toObject() : o;
  auto u = f.value("url").toString();
  if (!validMediaUrl(u, provider) || f.value("has_drm").toBool() ||
      (!(hasTrack(f, "vcodec") && hasTrack(f, "acodec")) &&
       !twitchClipFormat(o, f, provider) && !kickClipFormat(o, f, provider)) ||
      (provider == MediaProvider::Kick && !hls(f)) ||
      (provider == MediaProvider::Twitch &&
       (o.value("extractor_key").toString() == "TwitchClips"
            ? f.value("protocol").toString() != "https"
            : !hls(f)))) {
    m.error = "No playable combined video/audio stream";
    return m;
  }
  m.direct = u;
  m.hlsDirect = hls(f);
  return m;
}

Media selectMedia(const QByteArray &bytes, int maxHeight,
                  const std::function<bool(const QString &, bool)> &probe,
                  MediaProvider provider) {
  auto j = QJsonDocument::fromJson(bytes);
  if (!j.isObject())
    return parseMedia(bytes, provider);
  auto root = j.object();
  std::vector<QJsonObject> video, audio, combined;
  for (auto item : root.value("formats").toArray()) {
    auto f = item.toObject();
    if (!validMediaUrl(f.value("url").toString(), provider) ||
        f.value("has_drm").toBool())
      continue;
    bool kickClip = kickClipFormat(root, f, provider);
    bool clip = twitchClipFormat(root, f, provider) || kickClip;
    bool v = hasTrack(f, "vcodec") || clip, a = hasTrack(f, "acodec") || clip;
    int h = f.value("height").toInt();
    if (v && ((h <= 0 && !kickClip) || h > maxHeight))
      continue;
    if (v && a && (hls(f) || f.value("protocol").toString() == "https"))
      combined.push_back(f);
    else if (provider == MediaProvider::YouTube && hls(f) && v && !a &&
             f.value("vcodec").toString().startsWith("avc1"))
      video.push_back(f);
    else if (provider == MediaProvider::YouTube && hls(f) &&
             f.value("vcodec").toString() == "none" &&
             f.value("acodec").toString() != "none")
      audio.push_back(f);
  }
  auto quality = [](const QJsonObject &a, const QJsonObject &b) {
    auto score = [](const QJsonObject &f) {
      return f.value("height").toDouble() * 1000000 +
             f.value("fps").toDouble() * 10000 + f.value("tbr").toDouble();
    };
    return score(a) > score(b);
  };
  std::stable_sort(video.begin(), video.end(), quality);
  std::stable_sort(combined.begin(), combined.end(), quality);
  // yt-dlp orders equal-score formats worst to best.
  std::reverse(audio.begin(), audio.end());
  std::stable_sort(audio.begin(), audio.end(), [](auto &a, auto &b) {
    auto score = [](const QJsonObject &f) {
      return f.value("abr").toDouble(f.value("tbr").toDouble());
    };
    return score(a) > score(b);
  });
  // Audio must be reachable before starting a video-only player.
  QJsonObject a;
  for (size_t i = 0; i < audio.size() && i < 6; i++) {
    auto f = audio[i];
    if (probe(f.value("url").toString(), true)) {
      a = f;
      break;
    }
  }
  if (!a.isEmpty())
    for (size_t i = 0; i < video.size() && i < 5; i++) {
      auto v = video[i];
      if (!probe(v.value("url").toString(), true))
        continue;
      auto selected = root;
      selected.insert("requested_formats", QJsonArray{v, a});
      auto m = parseMedia(
          QJsonDocument(selected).toJson(QJsonDocument::Compact), provider);
      if (m.error.isEmpty())
        return m;
    }
  for (size_t i = 0; i < combined.size() && i < 4; i++) {
    auto f = combined[i];
    if (!probe(f.value("url").toString(), hls(f)))
      continue;
    auto selected = root;
    selected.insert("requested_formats", QJsonArray{f});
    auto m = parseMedia(QJsonDocument(selected).toJson(QJsonDocument::Compact),
                        provider);
    if (m.error.isEmpty())
      return m;
  }
  Media m;
  m.error = "No reachable video and audio rendition";
  return m;
}

bool validVodPlaylist(const QByteArray &body) {
  if (!body.startsWith("#EXTM3U") || body.size() > 1048576)
    return false;
  bool end = false, segment = false;
  for (auto line : body.split('\n')) {
    line = line.trimmed();
    if (line.isEmpty())
      continue;
    if (line == "#EXT-X-ENDLIST")
      end = true;
    if (line.startsWith('#')) {
      // YouTube's plain VOD media playlists do not require URI-bearing tags.
      if (line.contains("URI=") || line.startsWith("#EXT-X-STREAM-INF") ||
          line.startsWith("#EXT-X-I-FRAME-STREAM-INF"))
        return false;
    } else {
      if (!validMediaUrl(QString::fromUtf8(line)))
        return false;
      segment = true;
    }
  }
  return end && segment;
}

Media selectAudioMedia(const QByteArray &bytes,
                       const std::function<bool(const QString &, bool)> &probe,
                       MediaProvider provider) {
  Media result;
  result.error = "No reachable dedicated audio stream";
  auto root = QJsonDocument::fromJson(bytes).object();
  const auto key = root.value("extractor_key").toString();
  if (root.value("is_live").toBool() || root.value("has_drm").toBool() ||
      !((provider == MediaProvider::YouTube && key == "Youtube") ||
        (provider == MediaProvider::SoundCloud && key == "Soundcloud") ||
        (provider == MediaProvider::Bandcamp && key == "Bandcamp")))
    return result;
  std::vector<QJsonObject> formats;
  auto items = root.value("formats").toArray();
  for (int i = 0; i < items.size() && i < 1000; ++i) {
    auto f = items[i].toObject();
    auto protocol = f.value("protocol").toString();
    auto ext = f.value("ext").toString();
    auto codec = f.value("acodec").toString();
    bool scHls = provider == MediaProvider::SoundCloud &&
                 protocol == "m3u8_native" && ext == "m4a" &&
                 codec.startsWith("mp4a.") &&
                 QUrl(f.value("url").toString()).host() ==
                     "playback.media-streaming.soundcloud.cloud";
    if (f.value("has_drm").toBool() || f.value("vcodec").toString() != "none" ||
        codec.isEmpty() || codec == "none" ||
        !validMediaUrl(f.value("url").toString(), provider) ||
        !(protocol == "https" ||
          (provider == MediaProvider::SoundCloud && protocol == "http") ||
          scHls))
      continue;
    if (provider == MediaProvider::Bandcamp &&
        f.value("format_id").toString() != "mp3-128")
      continue;
    if (provider == MediaProvider::YouTube && !(ext == "m4a" || ext == "webm"))
      continue;
    if (audioOnlyProvider(provider) && ext != "mp3" && !scHls)
      continue;
    if (f.value("audio_channels").toInt(2) > 2)
      continue;
    if (f.value("format_id").toString().contains("-drc"))
      continue;
    formats.push_back(f);
  }
  std::stable_sort(formats.begin(), formats.end(),
                   [](const auto &a, const auto &b) {
                     return a.value("abr").toDouble(a.value("tbr").toDouble()) >
                            b.value("abr").toDouble(b.value("tbr").toDouble());
                   });
  for (size_t i = 0; i < formats.size() && i < 6; ++i) {
    auto url = formats[i].value("url").toString();
    bool manifest = hls(formats[i]);
    if (!probe(url, manifest))
      continue;
    result.hlsDirect = manifest;
    result.direct = url;
    result.info = normalizedMediaInfo(root, provider);
    result.title = result.info.value("title").toString();
    result.error.clear();
    return result;
  }
  return result;
}
