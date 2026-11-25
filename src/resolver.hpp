#pragma once
#include <QByteArray>
#include <QString>
enum class MediaProvider { Unknown, YouTube, Twitch };
MediaProvider mediaProvider(const QString &url);
QString canonicalTwitchUrl(const QString &url);
bool validTwitchLivePlaylist(const QByteArray &body);
struct Media {
  QString title, direct, manifest, error, videoUrl, audioUrl;
  QByteArray videoPlaylist, audioPlaylist;
  bool live = false;
  bool hlsDirect = false;
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
