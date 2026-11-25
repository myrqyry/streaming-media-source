#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>

struct RecoveryAttempt {
  int attempt;
  int64_t position;
};

// One bounded retry budget per explicitly selected video. No sleeping on
// OBS threads: the caller consumes due attempts on its normal video tick.
class RecoveryPlan {
  int used = 0;
  int64_t due = 0;
  std::optional<RecoveryAttempt> pending;

public:
  int attempts() const { return used; }
  bool waiting() const { return pending.has_value(); }
  void reset() {
    used = 0;
    due = 0;
    pending.reset();
  }
  bool schedule(int64_t now, int64_t position, bool live, bool enabled) {
    if (!enabled || pending || used >= 2)
      return false;
    ++used;
    due = now + (used == 1 ? 1000 : 3000);
    pending = RecoveryAttempt{used, live ? 0 : std::max<int64_t>(0, position)};
    return true;
  }
  std::optional<RecoveryAttempt> take(int64_t now) {
    if (!pending || now < due)
      return {};
    auto attempt = pending;
    pending.reset();
    return attempt;
  }
};

// OBS's native decoder reports stream timestamps, not elapsed VOD time.
// Capture the first observed decoded timestamp for the current epoch.
// This is a sampled origin, with accuracy bounded by the decoder's initial
// frame and the source tick cadence.
class VodClock {
  std::atomic<int64_t> origin{-1};

public:
  void reset() { origin.store(-1); }
  bool ready() const { return origin.load() >= 0; }
  bool observe(int64_t raw) {
    int64_t empty = -1;
    return raw > 0 && origin.compare_exchange_strong(empty, raw);
  }
  int64_t position(int64_t raw) const {
    auto start = origin.load();
    return start < 0 ? 0 : std::max<int64_t>(0, raw - start);
  }
  int64_t target(int64_t elapsed) const {
    auto start = origin.load();
    return start < 0 ? 0 : start + std::max<int64_t>(0, elapsed);
  }
};
