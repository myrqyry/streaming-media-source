#include "media-queue.hpp"
#include <QCoreApplication>
#include <cstdlib>
#include <iostream>
void check(bool yes, const char *msg) {
  if (!yes) {
    std::cerr << msg << "\n";
    std::exit(1);
  }
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QString a = "https://youtu.be/aqz-KE-bpKQ",
          b = "https://www.youtube.com/watch?v=BaW_jenozKc",
          c = "https://youtu.be/jNQXAC9IVRw";
  MediaQueue q;
  check(q.append(a) && q.append(b) && q.append(c), "append valid videos");
  check(!q.append("https://evil.test/watch?v=x"), "reject unsupported site");
  auto saved = q.serialize();
  check(MediaQueue::fromJson(saved).items() == q.items(), "roundtrip");
  check(q.move(2, 0) && q.items().first() == c, "reorder");
  check(q.take(1) == a && q.items() == QStringList({c, b}),
        "play selected removes only selected");
  check(q.take(0) == c && q.take(0) == b && q.take(0).isEmpty(),
        "FIFO with empty boundary");
  check(MediaQueue::fromJson("[1,\"bad\"]").items().isEmpty(),
        "reject malformed entries");
  check(videoId(a) == "aqz-KE-bpKQ" && videoId(b) == "BaW_jenozKc",
        "video IDs");
  check(videoId("https://youtube.com.evil.test/watch?v=BaW_jenozKc").isEmpty(),
        "host spoof");
  check(videoId("https://www.youtube.com/shorts/jNQXAC9IVRw") == "jNQXAC9IVRw",
        "shorts");
  check(reachedVodEnd(634000, 634566, false), "known natural VOD end");
  check(!reachedVodEnd(45000, 634566, false), "early disconnect is not end");
  check(!reachedVodEnd(0, 0, false), "unknown duration is not completion");
  check(!reachedVodEnd(634000, 634566, true),
        "live stream is not completed VOD");
  check(!reachedVodEnd(0, 2000, false),
        "short video not completed before progress");
}
