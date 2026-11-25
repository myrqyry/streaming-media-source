#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
enum class MediaProvider {
  Unknown,
  YouTube,
  Twitch,
  Kick,
  SoundCloud,
  Bandcamp,
  TikTok
};
MediaProvider mediaProvider(const QString &url);
QString canonicalKickUrl(const QString &url);
QString canonicalProviderUrl(const QString &url);
QString providerExtractor(const QString &url);
QString providerExtractorKey(const QString &url);
bool validLivePlaylist(const QByteArray &body, MediaProvider provider);
QByteArray normalizeFinitePlaylist(const QByteArray &body, const QString &base,
                                   MediaProvider provider);
QString canonicalTwitchUrl(const QString &url);
QString twitchExtractor(const QString &url);
QString twitchExtractorKey(const QString &url);
QByteArray normalizeTwitchVodPlaylist(const QByteArray &body,
                                      const QString &base);
struct TwitchVodSlice {
  QByteArray manifest;
  qint64 startMs = 0, durationMs = 0;
};
TwitchVodSlice sliceFinitePlaylist(const QByteArray &normalized,
                                   qint64 targetMs, bool allowTs);
TwitchVodSlice sliceTwitchFmp4Playlist(const QByteArray &normalized,
                                       qint64 targetMs);
bool validTwitchLivePlaylist(const QByteArray &body);
struct Media {
  QString title, direct, manifest, error, videoUrl, audioUrl;
  QByteArray videoPlaylist, audioPlaylist;
  QJsonObject info;
  bool live = false;
  bool hlsDirect = false;
  bool segmentVod = false;
  qint64 segmentStartMs = 0, segmentDurationMs = 0;
};
bool validYoutube(const QString &url);
bool validMediaUrl(const QString &url,
                   MediaProvider provider = MediaProvider::YouTube);
Media parseMedia(const QByteArray &json,
                 MediaProvider provider = MediaProvider::YouTube);
#include <functional>
Media selectMedia(const QByteArray &json, int maxHeight,
                  const std::function<bool(const QString &, bool)> &probe,
                  MediaProvider provider = MediaProvider::YouTube);
bool validVodPlaylist(const QByteArray &body);

Media selectAudioMedia(const QByteArray &json,
                       const std::function<bool(const QString &, bool)> &probe,
                       MediaProvider provider);
bool audioOnlyProvider(MediaProvider provider);
