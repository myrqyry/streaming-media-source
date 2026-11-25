#include "playlist-import.hpp"
#include "media-queue.hpp"
#include "resolver.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>
#include <cmath>
QString canonicalPlaylistUrl(const QString &url, bool audioOnly) {
  if (audioOnly && url.size() <= 2048) {
    QUrl u(url, QUrl::StrictMode);
    if (u.isValid() && u.scheme() == "https" && u.userInfo().isEmpty() &&
        (u.port() == -1 || u.port() == 443) && !u.hasFragment() &&
        !url.contains('\\') && !url.contains('\r') && !url.contains('\n') &&
        !QUrlQuery(u).hasQueryItem("secret_token")) {
      auto path = u.path(QUrl::FullyEncoded);
      static const QRegularExpression sc(
          "^/([A-Za-z0-9_-]{1,100})/sets/([A-Za-z0-9_-]{1,200})/?$");
      static const QRegularExpression bc("^/album/([A-Za-z0-9_-]{1,200})/?$");
      auto m = sc.match(path);
      if (m.hasMatch()) {
        auto candidate = canonicalProviderUrl(
            "https://" + u.host() + "/" + m.captured(1) + "/" + m.captured(2));
        if (mediaProvider(candidate) == MediaProvider::SoundCloud)
          return "https://soundcloud.com/" + m.captured(1) + "/sets/" +
                 m.captured(2);
      }
      m = bc.match(path);
      if (m.hasMatch() && mediaProvider(canonicalProviderUrl(
                              "https://" + u.host() + "/track/" +
                              m.captured(1))) == MediaProvider::Bandcamp)
        return "https://" + u.host() + "/album/" + m.captured(1);
    }
  }
  if (url.size() > 2048 || !validYoutube(url))
    return {};
  QUrl u(url);
  if (u.path() != "/playlist" && videoId(url).isEmpty())
    return {};
  auto lists = QUrlQuery(u).allQueryItemValues("list");
  if (lists.size() != 1)
    return {};
  auto id = lists.first();
  static const QRegularExpression re(
      "^(PL|UU|OLAK5uy_|FL|LL)[A-Za-z0-9_-]{2,150}$");
  if (!re.match(id).hasMatch())
    return {};
  return "https://www.youtube.com/playlist?list=" + id;
}
QString playlistExtractor(const QString &url) {
  auto canonical = canonicalPlaylistUrl(url, true);
  if (canonical.isEmpty())
    return {};
  if (canonical.startsWith("https://soundcloud.com/"))
    return "soundcloud:set";
  if (QUrl(canonical).host().endsWith(".bandcamp.com"))
    return "bandcamp:album";
  return "youtube:tab";
}
ImportedPlaylist parseImportedPlaylist(const QByteArray &json,
                                       const QString &collection) {
  ImportedPlaylist r;
  if (json.size() > 4 * 1024 * 1024)
    return r;
  auto doc = QJsonDocument::fromJson(json);
  if (!doc.isObject())
    return r;
  auto obj = doc.object();
  if (obj.value("_type") != "playlist" || !obj.value("entries").isArray())
    return r;
  auto extractor = collection.isEmpty() ? QString("youtube:tab")
                                        : playlistExtractor(collection);
  if (extractor.isEmpty())
    return r;
  auto provider = extractor == "soundcloud:set"   ? MediaProvider::SoundCloud
                  : extractor == "bandcamp:album" ? MediaProvider::Bandcamp
                                                  : MediaProvider::YouTube;
  auto key = provider == MediaProvider::SoundCloud ? "SoundcloudSet"
             : provider == MediaProvider::Bandcamp ? "BandcampAlbum"
                                                   : "YoutubeTab";
  if (!collection.isEmpty() && obj.value("extractor_key") != key)
    return r;
  auto rootAvailability = obj.value("availability").toString();
  if (!rootAvailability.isEmpty() && rootAvailability != "public" &&
      rootAvailability != "unlisted")
    return r;
  r.total = obj.value("playlist_count").toInt(-1);
  auto entries = obj.value("entries").toArray();
  for (int i = 0; i < entries.size() && i < 101; ++i) {
    auto e = entries[i].toObject();
    auto url =
        provider == MediaProvider::YouTube
            ? "https://www.youtube.com/watch?v=" + e.value("id").toString()
            : canonicalProviderUrl(e.value("url").toString());
    bool valid = provider == MediaProvider::YouTube
                     ? !videoId(url).isEmpty() && e.value("ie_key") == "Youtube"
                     : !url.isEmpty() && mediaProvider(url) == provider &&
                           e.value("ie_key") == providerExtractorKey(url) &&
                           !QUrlQuery(QUrl(e.value("url").toString()))
                                .hasQueryItem("secret_token");
    if (provider == MediaProvider::Bandcamp &&
        QUrl(url).host() != QUrl(collection).host())
      valid = false;
    auto availability = e.value("availability").toString();
    auto title = e.value("title").toString();
    if (!valid ||
        (!availability.isEmpty() && availability != "public" &&
         availability != "unlisted") ||
        title == "[Private video]" || title == "[Deleted video]" ||
        title == "Private video" || title == "Deleted video") {
      ++r.skipped;
      continue;
    }
    auto author = e.value("channel").toString();
    if (author.isEmpty())
      author = e.value("uploader").toString();
    auto meta = parsePlaylistMetadata(
        QJsonDocument(QJsonObject{{"title", title}, {"author_name", author}})
            .toJson(QJsonDocument::Compact));
    auto sec = e.value("duration").toDouble(-1);
    if (std::isfinite(sec) && sec >= 0 && sec <= 7 * 24 * 3600)
      meta.durationMs = qint64(sec * 1000);
    if (provider != MediaProvider::YouTube) {
      meta.details = normalizedMediaInfo(e, provider);
      if (!meta.details.value("author").toString().isEmpty())
        meta.author = meta.details.value("author").toString();
      if (meta.details.value("thumbnail").toString().isEmpty()) {
        auto image = obj.value("thumbnail").toString();
        if (validThumbnailUrl(image, provider))
          meta.details.insert("thumbnail", image);
      }
    }
    r.entries.append({url, meta});
  }
  return r;
}
ImportResult applyPlaylistImport(MediaQueue &q, const QStringList &urls,
                                 bool play, bool audioOnly) {
  ImportResult r;
  QStringList valid;
  for (const auto &u : urls)
    if ((audioOnly ? validSourceMediaUrl(u, true) : !videoId(u).isEmpty()) &&
        valid.size() < 101)
      valid.append(u);
  if (play && !valid.isEmpty())
    r.playUrl = valid.takeFirst();
  if (play) {
    MediaQueue next;
    int remaining = 100 - q.items().size();
    for (const auto &u : valid)
      if (remaining > 0 && next.append(u)) {
        ++r.added;
        --remaining;
      }
    for (const auto &u : q.items())
      next.append(u);
    q = std::move(next);
  } else
    for (const auto &u : valid)
      if (q.append(u))
        ++r.added;
  return r;
}
