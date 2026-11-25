#include "media-queue.hpp"
#include "resolver.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
QString videoId(const QString &url) {
  if (!validYoutube(url))
    return {};
  QUrl u(url);
  QString id;
  auto parts = u.path().split('/', Qt::SkipEmptyParts);
  if (u.host() == "youtu.be" && parts.size() == 1)
    id = parts[0];
  else if (u.path() == "/watch")
    id = QUrlQuery(u).queryItemValue("v");
  else if (parts.size() == 2 &&
           (parts[0] == "shorts" || parts[0] == "live" || parts[0] == "embed"))
    id = parts[1];
  static const QRegularExpression re("^[A-Za-z0-9_-]{11}$");
  return re.match(id).hasMatch() ? id : QString{};
}
QString canonicalMediaUrl(const QString &url) {
  if (url.size() > 2048)
    return {};
  auto id = videoId(url);
  if (!id.isEmpty())
    return "https://www.youtube.com/watch?v=" + id;
  return canonicalProviderUrl(url);
}
bool validSourceMediaUrl(const QString &url, bool audioOnly) {
  if (canonicalMediaUrl(url).isEmpty())
    return false;
  auto p = mediaProvider(url);
  return audioOnly ? p == MediaProvider::YouTube || audioOnlyProvider(p)
                   : !audioOnlyProvider(p);
}
QString mediaLabel(const QString &url) {
  auto id = videoId(url);
  if (!id.isEmpty())
    return id;
  auto canonical = canonicalProviderUrl(url);
  if (canonical.isEmpty())
    return {};
  auto u = QUrl(canonical);
  if (mediaProvider(canonical) == MediaProvider::SoundCloud)
    return "SoundCloud · " + u.path().mid(1);
  if (mediaProvider(canonical) == MediaProvider::Bandcamp)
    return "Bandcamp · " + u.path().mid(7);
  if (mediaProvider(canonical) == MediaProvider::Kick)
    return (providerExtractor(canonical) == "kick:live"   ? "Kick · "
            : providerExtractor(canonical) == "kick:clip" ? "Kick clip · "
                                                          : "Kick VOD · ") +
           u.path().mid(1);
  return u.host() == "clips.twitch.tv"     ? "Twitch clip · " + u.path().mid(1)
         : u.path().startsWith("/videos/") ? "Twitch VOD · " + u.path().mid(8)
                                           : "Twitch · " + u.path().mid(1);
}
bool MediaQueue::append(const QString &url) {
  if (urls.size() >= 100 || canonicalMediaUrl(url.trimmed()).isEmpty())
    return false;
  auto twitch = canonicalProviderUrl(url.trimmed());
  urls.append(twitch.isEmpty() ? url.trimmed() : twitch);
  return true;
}
bool MediaQueue::move(int from, int to) {
  if (from < 0 || to < 0 || from >= urls.size() || to >= urls.size() ||
      from == to)
    return false;
  urls.move(from, to);
  return true;
}
QString MediaQueue::take(int i) {
  return i < 0 || i >= urls.size() ? QString{} : urls.takeAt(i);
}
MediaQueue MediaQueue::fromJson(const QString &json) {
  MediaQueue q;
  if (json.size() > 100000)
    return q;
  auto d = QJsonDocument::fromJson(json.toUtf8());
  if (!d.isArray())
    return q;
  for (auto v : d.array())
    if (v.isString())
      q.append(v.toString());
  return q;
}
QString MediaQueue::serialize() const {
  return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(urls))
                               .toJson(QJsonDocument::Compact));
}

qint64 kickClipDuration(qint64 decoder, qint64 reported) {
  // Kick clip HLS can include a small amount of segment padding. Only
  // accept a positive, independently reported clip duration within 3s.
  return decoder > 0 && reported > 0 && reported <= decoder &&
                 decoder - reported <= 3000
             ? reported
             : decoder;
}
bool reachedVodEnd(qint64 position, qint64 duration, bool live) {
  if (live || duration <= 0 || position <= 0)
    return false;
  const auto tolerance = std::clamp<qint64>(duration / 20, 250, 3000);
  return position >= duration - tolerance;
}

QString MediaQueue::appendRequest(const QString &url, const QString &current,
                                  bool enabled) {
  if (!enabled)
    return "disabled";
  auto id = canonicalMediaUrl(url.trimmed());
  if (url.size() > 2048 || id.isEmpty())
    return "invalid_url";
  if (canonicalMediaUrl(current) == id)
    return "duplicate";
  for (const auto &item : urls)
    if (canonicalMediaUrl(item) == id)
      return "duplicate";
  if (urls.size() >= 100)
    return "queue_full";
  return append(id) ? "queued" : "invalid_url";
}

void VodProgress::observe(qint64 ms) {
  if (ms < 0)
    return;
  if (target >= 0) {
    if (ms < target - 2000 || ms > target + 2000)
      return;
    target = -1;
  }
  high = std::max(high, ms);
}
