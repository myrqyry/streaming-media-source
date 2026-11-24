#include "resolver.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <algorithm>
bool validYoutube(const QString &s) {
  QUrl u(s, QUrl::StrictMode);
  auto h = u.host().toLower();
  return u.isValid() && u.scheme() == "https" && u.userInfo().isEmpty() &&
         (u.port() == -1 || u.port() == 443) &&
         (h == "youtube.com" || h == "www.youtube.com" ||
          h == "m.youtube.com" || h == "youtu.be");
}
static bool mediaUrl(const QString &s) {
  QUrl u(s, QUrl::StrictMode);
  auto h = u.host().toLower();
  return u.isValid() && u.scheme() == "https" && u.userInfo().isEmpty() &&
         (u.port() == -1 || u.port() == 443) &&
         (h == "googlevideo.com" || h.endsWith(".googlevideo.com")) &&
         !s.contains('"') && !s.contains('\n') && !s.contains('\r') &&
         !s.contains('\\');
}
static bool hls(const QJsonObject &o) {
  return o.value("protocol").toString().startsWith("m3u8");
}
static bool hasTrack(const QJsonObject &o, const char *key) {
  auto s = o.value(key).toString();
  return !s.isEmpty() && s != "none";
}
Media parseMedia(const QByteArray &bytes) {
  Media m;
  QJsonParseError e;
  auto j = QJsonDocument::fromJson(bytes, &e);
  if (e.error != QJsonParseError::NoError || !j.isObject()) {
    m.error = "Invalid extractor response";
    return m;
  }
  auto o = j.object();
  m.title = o.value("title").toString();
  m.live = o.value("is_live").toBool();
  auto tracks = o.value("requested_formats").toArray();
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
    if (!hls(v) || !hls(a) || !mediaUrl(vu) || !mediaUrl(au) || w <= 0 ||
        h <= 0 || w > 7680 || h > 4320) {
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
  if (!mediaUrl(u) || !hasTrack(f, "vcodec") || !hasTrack(f, "acodec")) {
    m.error = "No playable combined video/audio stream";
    return m;
  }
  m.direct = u;
  return m;
}

Media selectMedia(const QByteArray &bytes, int maxHeight,
                  const std::function<bool(const QString &, bool)> &probe) {
  auto j = QJsonDocument::fromJson(bytes);
  if (!j.isObject())
    return parseMedia(bytes);
  auto root = j.object();
  std::vector<QJsonObject> video, audio, combined;
  for (auto item : root.value("formats").toArray()) {
    auto f = item.toObject();
    if (!mediaUrl(f.value("url").toString()) || f.value("has_drm").toBool())
      continue;
    bool v = hasTrack(f, "vcodec"), a = hasTrack(f, "acodec");
    int h = f.value("height").toInt();
    if (v && (h <= 0 || h > maxHeight))
      continue;
    if (v && a && (hls(f) || f.value("protocol").toString() == "https"))
      combined.push_back(f);
    else if (hls(f) && v && !a &&
             f.value("vcodec").toString().startsWith("avc1"))
      video.push_back(f);
    else if (hls(f) && f.value("vcodec").toString() == "none" &&
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
      auto m =
          parseMedia(QJsonDocument(selected).toJson(QJsonDocument::Compact));
      if (m.error.isEmpty())
        return m;
    }
  for (size_t i = 0; i < combined.size() && i < 4; i++) {
    auto f = combined[i];
    if (!probe(f.value("url").toString(), hls(f)))
      continue;
    auto selected = root;
    selected.insert("requested_formats", QJsonArray{f});
    auto m = parseMedia(QJsonDocument(selected).toJson(QJsonDocument::Compact));
    if (m.error.isEmpty())
      return m;
  }
  Media m;
  m.error = "No reachable YouTube video and audio rendition";
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
      if (!mediaUrl(QString::fromUtf8(line)))
        return false;
      segment = true;
    }
  }
  return end && segment;
}
