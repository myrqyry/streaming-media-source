#include "playlist-metadata.hpp"
#include <QJsonDocument>
#include <QJsonObject>
PlaylistMetadata parsePlaylistMetadata(const QByteArray &bytes) {
  if (bytes.size() > 65536)
    return {};
  auto json = QJsonDocument::fromJson(bytes);
  if (!json.isObject())
    return {};
  auto obj = json.object();
  auto clean = [](QString text) {
    QString result;
    for (auto ch : text.left(600)) {
      if (ch.isSpace())
        result += ' ';
      else if (ch.category() != QChar::Other_Control &&
               ch.category() != QChar::Other_Format)
        result += ch;
    }
    return result.simplified().left(300);
  };
  return {clean(obj.value("title").toString()),
          clean(obj.value("author_name").toString())};
}
int playlistDropTarget(int from, int insertion, int count) {
  if (from < 0 || from >= count || insertion < 0 || insertion > count)
    return -1;
  return insertion > from ? insertion - 1 : insertion;
}
