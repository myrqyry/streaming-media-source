#pragma once
#include <QByteArray>
#include <QString>
struct PlaylistMetadata {
  QString title, author;
  qint64 durationMs = -1;
};
PlaylistMetadata parsePlaylistMetadata(const QByteArray &bytes);
int playlistDropTarget(int from, int insertion, int count);
