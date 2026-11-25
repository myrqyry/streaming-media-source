#include "playlist-metadata.hpp"
#include "resolver.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdlib>
#include <iostream>
static void check(bool ok, const char *msg) {
  if (!ok) {
    std::cerr << msg << '\n';
    std::exit(1);
  }
}
int main() {
  auto meta = parsePlaylistMetadata(
      R"({"title":"  A\n real video ","author_name":"Creator","thumbnail_url":"https://untrusted.invalid/x"})");
  check(meta.title == "A real video" && meta.author == "Creator",
        "bounded title and author only");
  check(parsePlaylistMetadata("[]").title.isEmpty(), "reject non-object");
  check(parsePlaylistMetadata(R"({"title":42})").title.isEmpty(),
        "reject non-string title");
  check(parsePlaylistMetadata(QByteArray(65537, 'x')).title.isEmpty(),
        "reject oversized metadata");
  auto longTitle = parsePlaylistMetadata(QByteArray("{\"title\":\"") +
                                         QByteArray(800, 'a') + "\"}");
  check(longTitle.title.size() <= 300, "truncate title");
  check(playlistDropTarget(0, 3, 3) == 2, "move first to end");
  check(playlistDropTarget(2, 0, 3) == 0, "move last to beginning");
  check(playlistDropTarget(1, 2, 3) == 1, "same slot remains unchanged");
  check(playlistDropTarget(1, 0, 0) == -1, "empty queue rejected");
  check(playlistDropTarget(4, 0, 3) == -1, "stale row rejected");
  check(playlistDropTarget(0, 4, 3) == -1, "invalid insertion rejected");
  auto info = normalizedMediaInfo(
      QJsonDocument::fromJson(
          R"({"title":"  Clip ","channel":"Streamer","uploader":"Clipper","description":"Hello\n world","duration":60,"view_count":0,"like_count":null,"categories":["Music"],"thumbnail":"https://static-cdn.jtvnw.net/clip.jpg","url":"https://private.example/token","formats":[{"height":720,"url":"https://private.example/secret"}]})")
          .object(),
      MediaProvider::Twitch);
  check(info.value("author").toString() == "Streamer" &&
            info.value("uploader").toString() == "Clipper",
        "channel and clip creator");
  check(info.value("views").toDouble(-1) == 0 && !info.contains("likes"),
        "zero versus missing count");
  check(info.value("durationMs").toDouble() == 60000 &&
            info.value("category").toString() == "Music",
        "duration and category");
  check(!info.contains("url") && !info.contains("formats") &&
            !QJsonDocument(info).toJson().contains("private.example"),
        "never propagate signed media URLs or raw formats");
  check(validThumbnailUrl(info.value("thumbnail").toString(),
                          MediaProvider::Twitch),
        "observed Twitch thumbnail host");
  check(!validThumbnailUrl("https://static-cdn.jtvnw.net.evil.test/img",
                           MediaProvider::Twitch),
        "spoofed host");
  check(!validThumbnailUrl("https://static-cdn.jtvnw.net@evil.test/img",
                           MediaProvider::Twitch),
        "userinfo rejected");
  check(!validThumbnailUrl("https://i.ytimg.com/img", MediaProvider::Twitch),
        "cross provider thumbnail rejected");
}
