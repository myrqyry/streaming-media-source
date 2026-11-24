#include "playlist-import.hpp"
#include "media-queue.hpp"
#include "resolver.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>
#include <cmath>
QString canonicalPlaylistUrl(const QString &url) {
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
ImportedPlaylist parseImportedPlaylist(const QByteArray &json) {
  ImportedPlaylist r;
  if (json.size() > 4 * 1024 * 1024)
    return r;
  auto doc = QJsonDocument::fromJson(json);
  if (!doc.isObject())
    return r;
  auto obj = doc.object();
  if (obj.value("_type") != "playlist" || !obj.value("entries").isArray())
    return r;
  r.total = obj.value("playlist_count").toInt(-1);
  auto entries = obj.value("entries").toArray();
  for (int i = 0; i < entries.size() && i < 101; ++i) {
    auto e = entries[i].toObject();
    auto url = "https://www.youtube.com/watch?v=" + e.value("id").toString();
    auto availability = e.value("availability").toString();
    auto title = e.value("title").toString();
    if (videoId(url).isEmpty() || e.value("ie_key") != "Youtube" ||
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
    r.entries.append({url, meta});
  }
  return r;
}
ImportResult applyPlaylistImport(MediaQueue &q, const QStringList &urls,
                                 bool play) {
  ImportResult r;
  QStringList valid;
  for (const auto &u : urls)
    if (!videoId(u).isEmpty() && valid.size() < 101)
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
