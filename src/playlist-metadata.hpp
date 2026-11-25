#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
enum class MediaProvider;
struct PlaylistMetadata {
  QString title, author;
  qint64 durationMs = -1;
  QJsonObject details;
};
PlaylistMetadata parsePlaylistMetadata(const QByteArray &bytes);
int playlistDropTarget(int from, int insertion, int count);

bool validThumbnailUrl(const QString &url, MediaProvider provider);
QJsonObject normalizedMediaInfo(const QJsonObject &root,
                                MediaProvider provider);
