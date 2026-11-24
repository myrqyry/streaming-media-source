#include "media-queue.hpp"
#include "playlist-import.hpp"
#include <cstdlib>
#include <iostream>
static void check(bool b, const char *msg) {
  if (!b) {
    std::cerr << msg << '\n';
    std::exit(1);
  }
}
int main() {
  auto link = QString("https://www.youtube.com/playlist?list=PL123abc");
  check(canonicalPlaylistUrl(link) == link, "playlist URL");
  check(canonicalPlaylistUrl("https://youtu.be/jNQXAC9IVRw?list=PL123abc") ==
            link,
        "mixed URL");
  check(canonicalPlaylistUrl(
            "https://youtube.com.evil.test/playlist?list=PL123abc")
            .isEmpty(),
        "reject spoof");
  check(canonicalPlaylistUrl("https://youtube.com/playlist?list=RD123abc")
            .isEmpty(),
        "reject dynamic mixes");
  check(canonicalPlaylistUrl(
            "https://youtube.com/playlist?list=PL123abc&list=PLother")
            .isEmpty(),
        "ambiguous list rejected");
  auto parsed = parseImportedPlaylist(
      R"({"_type":"playlist","playlist_count":9,"entries":[{"id":"jNQXAC9IVRw","title":" First ","channel":"Channel","duration":19,"ie_key":"Youtube"},{"id":"aqz-KE-bpKQ","title":"Private video","availability":"private","ie_key":"Youtube"},null,{"id":"BaW_jenozKc","title":"Second","duration":null,"ie_key":"Youtube"},{"id":"aqz-KE-bpKQ","ie_key":"Other"}]})");
  check(parsed.entries.size() == 2 && parsed.skipped == 3 && parsed.total == 9,
        "skip inaccessible or non-video entries");
  check(parsed.entries[0].metadata.title == "First" &&
            parsed.entries[0].metadata.durationMs == 19000 &&
            parsed.entries[1].metadata.durationMs == -1,
        "metadata and unknown duration");
  check(parseImportedPlaylist(R"({"entries":[{"id":"jNQXAC9IVRw"}]})")
            .entries.isEmpty(),
        "reject non-playlist response");
  QStringList input{"https://youtu.be/jNQXAC9IVRw",
                    "https://youtu.be/BaW_jenozKc"};
  MediaQueue q;
  q.append("https://youtu.be/aqz-KE-bpKQ");
  auto result = applyPlaylistImport(q, input, false);
  check(result.added == 2 && result.playUrl.isEmpty() &&
            q.items() == QStringList({"https://youtu.be/aqz-KE-bpKQ", input[0],
                                      input[1]}),
        "queue append order");
  MediaQueue p;
  p.append("https://youtu.be/aqz-KE-bpKQ");
  result = applyPlaylistImport(p, input, true);
  check(result.playUrl == input[0] && result.added == 1 &&
            p.items() ==
                QStringList({input[1], "https://youtu.be/aqz-KE-bpKQ"}),
        "play first, prepend remainder, preserve old queue");
  MediaQueue full;
  for (int i = 0; i < 100; ++i)
    full.append(input[0]);
  result = applyPlaylistImport(full, input, false);
  check(result.added == 0 && full.items().size() == 100,
        "full queue unchanged");
  result = applyPlaylistImport(full, input, true);
  check(result.playUrl == input[0] && result.added == 0 &&
            full.items().size() == 100,
        "play without evicting queued entries");
}
