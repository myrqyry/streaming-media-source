#include "audio-handoff.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
using namespace std::chrono_literals;
void check(bool ok, const char *msg) {
  if (!ok) {
    std::cerr << msg << "\n";
    std::exit(1);
  }
}
int main() {
  std::mutex m;
  std::condition_variable cv;
  bool entered = false, release = false;
  std::atomic<int> delivered{0};
  std::atomic<float> sample{0};
  std::atomic<uint64_t> timestamp{0};
  AudioHandoff bridge([&](const AudioPacket &p) {
    std::unique_lock<std::mutex> l(m);
    if (!entered) {
      entered = true;
      cv.notify_all();
      cv.wait(l, [&] { return release; });
    }
    sample = p.planes[0][0];
    timestamp = p.timestamp;
    ++delivered;
    cv.notify_all();
  });
  float a[4] = {.25f, .5f, .75f, 1};
  const uint8_t *planes[MAX_AV_PLANES] = {reinterpret_cast<uint8_t *>(a),
                                          reinterpret_cast<uint8_t *>(a)};
  bridge.reset(1);
  check(bridge.push(planes, 4, 123, 48000, SPEAKERS_STEREO, 1),
        "first packet admitted");
  {
    std::unique_lock<std::mutex> l(m);
    check(cv.wait_for(l, 2s, [&] { return entered; }), "sink entered");
  }
  // Sink is blocked as it would be waiting for a parent OBS lock.
  auto begin = std::chrono::steady_clock::now();
  check(bridge.push(planes, 4, 456, 48000, SPEAKERS_STEREO, 1),
        "capture callback remains independent of blocked sink");
  check(std::chrono::steady_clock::now() - begin < 100ms,
        "capture does not wait on sink");
  bridge.reset(2);
  check(!bridge.push(planes, 4, 789, 48000, SPEAKERS_STEREO, 1),
        "stale generation rejected");
  check(bridge.push(planes, 4, 999, 48000, SPEAKERS_STEREO, 2),
        "new generation accepted");
  a[0] = 0;
  {
    std::lock_guard<std::mutex> l(m);
    release = true;
  }
  cv.notify_all();
  {
    std::unique_lock<std::mutex> l(m);
    check(cv.wait_for(l, 2s, [&] { return delivered == 2; }),
          "only in-flight and new packet delivered");
  }
  check(sample == .25f && timestamp == 999, "owned PCM and original timestamp");
  bridge.stop();
  check(!bridge.push(planes, 4, 1000, 48000, SPEAKERS_STEREO, 2),
        "stop rejects new capture");
}
