#include "playlist-metadata.hpp"
#include "resolver.hpp"
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <cmath>
static QString clean(QString text, int limit = 300) {
  QString result;
  for (auto ch : text.left(limit * 2)) {
    if (ch.isSpace())
      result += ' ';
    else if (ch.category() != QChar::Other_Control &&
             ch.category() != QChar::Other_Format)
      result += ch;
  }
  return result.simplified().left(limit);
}
bool validThumbnailUrl(const QString &url, MediaProvider provider) {
  QUrl u(url, QUrl::StrictMode);
  if (url.size() > 2048 || !u.isValid() || u.scheme() != "https" ||
      !u.userInfo().isEmpty() || (u.port() != -1 && u.port() != 443) ||
      u.hasFragment() || url.contains('\\') || url.contains('\n') ||
      url.contains('\r'))
    return false;
  auto h = u.host().toLower();
  if (provider == MediaProvider::SoundCloud)
    return h == "i1.sndcdn.com";
  if (provider == MediaProvider::Bandcamp)
    return h == "f4.bcbits.com";
  if (provider == MediaProvider::Kick)
    return h == "stream.kick.com" || h == "clips.kick.com";
  return provider == MediaProvider::Twitch
             ? h == "static-cdn.jtvnw.net"
             : provider == MediaProvider::YouTube &&
                   (h == "i.ytimg.com" || h == "i9.ytimg.com");
}
QJsonObject normalizedMediaInfo(const QJsonObject &root,
                                MediaProvider provider) {
  QJsonObject out;
  auto string = [&](const char *key, const QString &value, int limit = 300) {
    auto text = clean(value, limit);
    if (!text.isEmpty())
      out.insert(key, text);
  };
  string("title", root.value("title").toString());
  auto channel = root.value("channel").toString();
  auto uploader = root.value("uploader").toString();
  auto artist = root.value("artist").toString();
  string("author", !artist.isEmpty()   ? artist
                   : channel.isEmpty() ? uploader
                                       : channel);
  string("album", root.value("album").toString());
  string("track", root.value("track").toString());
  if (!channel.isEmpty() && uploader != channel)
    string("uploader", uploader);
  string("description", root.value("description").toString(), 1200);
  auto category = root.value("categories").toArray();
  QStringList categories;
  for (int i = 0; i < category.size() && i < 4; i++) {
    auto name = clean(category.at(i).toString(), 80);
    if (!name.isEmpty())
      categories << name;
  }
  string("category", categories.join(" · "));
  string("liveStatus", root.value("live_status").toString(), 40);
  out.insert("live", root.value("is_live").toBool());
  auto number = [&](const char *from, const char *to, double cap,
                    double scale = 1.0) {
    auto v = root.value(from).toDouble(-1);
    if (std::isfinite(v) && v >= 0 && v <= cap)
      out.insert(to, double(qint64(v * scale)));
  };
  if (!out.value("live").toBool())
    number("duration", "durationMs", 7 * 24 * 3600, 1000);
  number("view_count", "views", 1e15);
  number("concurrent_view_count", "viewers", 1e15);
  number("like_count", "likes", 1e15);
  number("channel_follower_count", "followers", 1e15);
  number("comment_count", "comments", 1e15);
  auto date =
      QDate::fromString(root.value("upload_date").toString(), "yyyyMMdd");
  if (date.isValid())
    out.insert("date", date.toString(Qt::ISODate));
  auto thumb = root.value("thumbnail").toString();
  if (validThumbnailUrl(thumb, provider))
    out.insert("thumbnail", thumb);
  QSet<int> heights;
  auto formats = root.value("formats").toArray();
  for (int i = 0; i < formats.size() && i < 1000; i++) {
    auto f = formats.at(i).toObject();
    auto h = f.value("height").toInt();
    if (h > 0 && h <= 4320 && !f.value("has_drm").toBool() &&
        f.value("vcodec").toString() != "none" &&
        validMediaUrl(f.value("url").toString(), provider))
      heights.insert(h);
  }
  auto list = heights.values();
  std::sort(list.begin(), list.end());
  QJsonArray resolutions;
  for (int h : list)
    resolutions.append(h);
  if (!resolutions.isEmpty())
    out.insert("resolutions", resolutions);
  return out;
}
PlaylistMetadata parsePlaylistMetadata(const QByteArray &bytes) {
  if (bytes.size() > 65536)
    return {};
  auto json = QJsonDocument::fromJson(bytes);
  if (!json.isObject())
    return {};
  auto obj = json.object();
  return {clean(obj.value("title").toString()),
          clean(obj.value("author_name").toString())};
}
int playlistDropTarget(int from, int insertion, int count) {
  if (from < 0 || from >= count || insertion < 0 || insertion > count)
    return -1;
  return insertion > from ? insertion - 1 : insertion;
}
