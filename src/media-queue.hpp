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

// A decoder may reset its clock before reporting EOF. Preserve observed
// progress, but require confirmation of an explicit seek before tracking again.
class VodProgress {
  qint64 high = 0, target = -1;

public:
  void reset() {
    high = 0;
    target = -1;
  }
  void seek(qint64 ms) {
    high = 0;
    target = ms < 0 ? 0 : ms;
  }
  void observe(qint64 ms);
  qint64 position() const { return high; }
};
