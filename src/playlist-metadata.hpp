#pragma once
#include <QByteArray>
#include <QString>
struct PlaylistMetadata {
  QString title, author;
};
PlaylistMetadata parsePlaylistMetadata(const QByteArray &bytes);
int playlistDropTarget(int from, int insertion, int count);
