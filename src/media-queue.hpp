#pragma once
#include <QStringList>
class MediaQueue {
  QStringList urls;

public:
  static MediaQueue fromJson(const QString &json);
  QString serialize() const;
  const QStringList &items() const { return urls; }
  bool append(const QString &url);
  QString appendRequest(const QString &url, const QString &current,
                        bool enabled);
  bool move(int from, int to);
  QString take(int index);
};
QString videoId(const QString &url);
bool reachedVodEnd(qint64 position, qint64 duration, bool live);
