#pragma once
#include <QByteArray>
#include <QString>
struct Media {
  QString title, direct, manifest, error, videoUrl, audioUrl;
  QByteArray videoPlaylist, audioPlaylist;
  bool live = false;
};
bool validYoutube(const QString &url);
bool validMediaUrl(const QString &url);
Media parseMedia(const QByteArray &json);
#include <functional>
Media selectMedia(const QByteArray &json, int maxHeight,
                  const std::function<bool(const QString &, bool)> &probe);
bool validVodPlaylist(const QByteArray &body);
