#pragma once
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <obs.h>
#include <thread>
#include <vector>

// The decoder invokes capture callbacks while holding its filter/audio locks.
// Never enter another OBS source from that callback. Copy owned PCM into a
// bounded queue and deliver it on an independent thread, outside queue locks.
struct AudioPacket {
  std::array<std::vector<float>, MAX_AV_PLANES> planes;
  uint32_t frames = 0, rate = 0;
  speaker_layout speakers = SPEAKERS_UNKNOWN;
  uint64_t timestamp = 0, generation = 0;
};
class AudioHandoff {
  static constexpr size_t Capacity = 16;
  static constexpr uint32_t MaxFrames = 8192;
  std::array<AudioPacket, Capacity> ring;
  size_t head = 0, count = 0;
  uint64_t generation = 0;
  bool closing = false;
  std::mutex mutex;
  std::condition_variable cv;
  std::function<void(const AudioPacket &)> sink;
  std::thread worker;
  void run() {
    AudioPacket p;
    for (auto &plane : p.planes)
      plane.reserve(MaxFrames);
    for (;;) {
      {
        std::unique_lock<std::mutex> l(mutex);
        cv.wait(l, [&] { return closing || count; });
        if (closing)
          return;
        std::swap(p, ring[head]);
        head = (head + 1) % Capacity;
        --count;
      }
      sink(p);
    }
  }

public:
  explicit AudioHandoff(std::function<void(const AudioPacket &)> output)
      : sink(std::move(output)) {
    for (auto &p : ring)
      for (auto &plane : p.planes)
        plane.reserve(MaxFrames);
    worker = std::thread([this] { run(); });
  }
  ~AudioHandoff() { stop(); }
  AudioHandoff(const AudioHandoff &) = delete;
  AudioHandoff &operator=(const AudioHandoff &) = delete;
  bool push(const uint8_t *const *data, uint32_t frames, uint64_t timestamp,
            uint32_t rate, speaker_layout speakers, uint64_t gen) {
    auto channels = get_audio_channels(speakers);
    if (!frames || frames > MaxFrames || !channels ||
        channels > MAX_AV_PLANES || !rate)
      return false;
    std::unique_lock<std::mutex> l(mutex, std::try_to_lock);
    if (!l || closing || gen != generation)
      return false;
    // Prefer current audio if the downstream source has fallen behind.
    if (count == Capacity) {
      head = (head + 1) % Capacity;
      --count;
    }
    auto &p = ring[(head + count) % Capacity];
    p.frames = frames;
    p.rate = rate;
    p.timestamp = timestamp;
    p.speakers = speakers;
    p.generation = gen;
    for (size_t i = 0; i < channels; ++i) {
      p.planes[i].resize(frames);
      if (data[i])
        std::memcpy(p.planes[i].data(), data[i], frames * sizeof(float));
      else
        std::fill(p.planes[i].begin(), p.planes[i].end(), 0.f);
    }
    ++count;
    l.unlock();
    cv.notify_one();
    return true;
  }
  void reset(uint64_t gen) {
    std::lock_guard<std::mutex> l(mutex);
    generation = gen;
    head = 0;
    count = 0;
  }
  void stop() {
    {
      std::lock_guard<std::mutex> l(mutex);
      closing = true;
      count = 0;
    }
    cv.notify_all();
    if (worker.joinable())
      worker.join();
  }
};
