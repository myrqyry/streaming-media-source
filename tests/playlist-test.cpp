#include "playlist-metadata.hpp"
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
}
