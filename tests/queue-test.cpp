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
  MediaQueue requests;
  check(requests.appendRequest(a, {}, false) == "disabled", "opt-in required");
  check(requests.appendRequest("https://evil.test/watch?v=x", {}, true) ==
            "invalid_url",
        "reject remote URL");
  check(requests.appendRequest(a, b, true) == "queued", "valid request");
  check(requests.items().first() ==
            "https://www.youtube.com/watch?v=aqz-KE-bpKQ",
        "canonical URL");
  check(
      requests.appendRequest("https://www.youtube.com/watch?v=aqz-KE-bpKQ&t=4",
                             {}, true) == "duplicate",
      "reject equivalent duplicate");
  check(requests.appendRequest(b, b, true) == "duplicate",
        "reject current video");
  check(requests.appendRequest(c, {}, true) == "queued",
        "second valid request");
  MediaQueue full;
  for (int i = 0; i < 100; i++)
    full.append(a);
  check(full.appendRequest(c, {}, true) == "queue_full", "bounded queue");
  MediaQueue twitch;
  auto clip = QString("https://clips.twitch.tv/ExampleClip-123");
  check(twitch.append("https://www.twitch.tv/LofiGirl"),
        "Twitch channel queue");
  check(twitch.appendRequest(clip, {}, true) == "queued",
        "Twitch clip request");
  check(twitch.appendRequest(
            "https://twitch.tv/user/clip/ExampleClip-123?foo=bar", {}, true) ==
            "duplicate",
        "canonical clip alias");
  check(twitch.appendRequest("https://twitch.tv/lofigirl", {}, true) ==
            "duplicate",
        "channel case identity");
  check(MediaQueue::fromJson(twitch.serialize()).items() == twitch.items(),
        "mixed provider persistence");
  check(twitch.appendRequest("https://m.twitch.tv/videos/123?t=2h", {}, true) ==
            "queued",
        "VOD queue request");
  check(twitch.appendRequest("https://www.twitch.tv/videos/123", {}, true) ==
            "duplicate",
        "VOD canonical duplicate");
  check(twitch.items().last() == "https://www.twitch.tv/videos/123",
        "canonical VOD persistence");
  check(MediaQueue::fromJson(twitch.serialize()).items() == twitch.items(),
        "mixed VOD queue roundtrip");
  check(twitch.appendRequest("https://www.kick.com/XQC", {}, true) == "queued",
        "Kick queue");
  check(twitch.appendRequest("https://kick.com/xqc", {}, true) == "duplicate",
        "Kick alias");
  check(MediaQueue::fromJson(twitch.serialize()).items() == twitch.items(),
        "three provider roundtrip");
  check(kickClipDuration(32000, 30000) == 30000, "Kick clip bounded padding");
  check(kickClipDuration(32000, 10000) == 32000,
        "reject large duration mismatch");
  check(kickClipDuration(32000, -1) == 32000, "missing clip duration");
  check(reachedVodEnd(29973, kickClipDuration(32000, 30000), false),
        "observed clip EOF");
  check(!reachedVodEnd(15000, kickClipDuration(32000, 30000), false),
        "early clip interruption");
  VodProgress progress;
  progress.observe(626590);
  progress.observe(633000);
  progress.observe(23);
  check(reachedVodEnd(progress.position(), 634566, false),
        "decoder clock reset preserves observed end");
  progress.seek(45000);
  progress.observe(633000);
  check(progress.position() == 0,
        "ignore old decoder position after backward seek");
  progress.observe(45030);
  progress.observe(46000);
  check(!reachedVodEnd(progress.position(), 634566, false),
        "backward seek does not advance queue");
  progress.reset();
  check(progress.position() == 0, "new playback clears progress");
}
