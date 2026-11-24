#pragma once
#include "playlist-metadata.hpp"
#include <QList>
#include <QStringList>
class MediaQueue;
struct ImportedEntry {
  QString url;
  PlaylistMetadata metadata;
};
struct ImportedPlaylist {
  QList<ImportedEntry> entries;
  int skipped = 0, total = -1;
};
struct ImportResult {
  QString playUrl;
  int added = 0;
};
QString canonicalPlaylistUrl(const QString &url);
ImportedPlaylist parseImportedPlaylist(const QByteArray &json);
ImportResult applyPlaylistImport(MediaQueue &queue, const QStringList &urls,
                                 bool play);
