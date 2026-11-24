#pragma once
#include <algorithm>
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
