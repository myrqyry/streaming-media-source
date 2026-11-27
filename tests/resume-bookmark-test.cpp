#include "resume-bookmark.hpp"
#include <iostream>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << __LINE__ << ": " << #x << "\n";                             \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main() {
  const QString id = "test-source",
                url = "https://www.youtube.com/watch?v=aqz-KE-bpKQ";
  ResumeBookmark b{ResumeMode::Paused, 42000};
  auto data = encodeResume(id, url, b);
  auto r = decodeResume(data, id, url);
  CHECK(r && r->mode == ResumeMode::Paused && r->position == 42000);
  CHECK(!decodeResume(data, "other-source", url));
  CHECK(!decodeResume(data, id, url + "2"));
  CHECK(!decodeResume("broken", id, url));
  CHECK(
      !decodeResume(encodeResume(id, url, {ResumeMode::Playing, -1}), id, url));
  CHECK(!decodeResume(
      encodeResume(id, url, {ResumeMode::Playing, 9000000000000LL}), id, url));
  CHECK(decodeResume(encodeResume(id, url, {ResumeMode::Stopped, 0}), id, url)
            ->mode == ResumeMode::Stopped);
  CHECK(decodeResume(encodeResume(id, url, {ResumeMode::Playing, 0}), id, url)
            ->position == 0);
  CHECK(!data.contains(url)); // no additional URL history
  return 0;
}
