#include "media-recovery.hpp"
#include <cstdlib>
#include <iostream>
static void check(bool ok, const char *msg) {
  if (!ok) {
    std::cerr << msg << '\n';
    std::exit(1);
  }
}
int main() {
  RecoveryPlan plan;
  check(!plan.schedule(100, 12000, false, false), "disabled recovery");
  check(plan.schedule(100, 12000, false, true), "first retry scheduled");
  check(!plan.take(1099), "first retry waits one second");
  auto first = plan.take(1100);
  check(first && first->position == 12000 && first->attempt == 1,
        "first retry preserves VOD position");
  check(!plan.take(2000), "retry consumed once");
  check(plan.schedule(3000, 14000, false, true), "second retry scheduled");
  check(!plan.take(5999), "second retry waits three seconds");
  auto second = plan.take(6000);
  check(second && second->position == 14000 && second->attempt == 2,
        "second retry preserves newer position");
  check(!plan.schedule(7000, 16000, false, true), "budget exhausted");
  plan.reset();
  check(plan.schedule(8000, 14000, true, true), "new live session can retry");
  auto live = plan.take(9000);
  check(live && live->position == 0, "live never seeks");
  plan.reset();
  check(plan.schedule(10000, 9000, false, true), "pending recovery");
  plan.reset();
  check(!plan.take(20000) && plan.attempts() == 0,
        "manual stop or new source cancels recovery");
  VodClock clock;
  check(clock.position(8502448) == 0 && !clock.ready(), "uninitialized clock");
  check(!clock.observe(0), "ignore decoder placeholder timestamp");
  check(clock.observe(8502448), "capture initial VOD timestamp");
  check(clock.position(8503448) == 1000 && clock.target(120000) == 8622448,
        "relative time and absolute seek");
  check(clock.position(8500000) == 0, "timestamp before origin");
  clock.observe(9000000);
  check(clock.target(0) == 8502448, "origin stable across seeks");
  clock.reset();
  check(!clock.ready() && clock.observe(62044) &&
            clock.target(120000) == 182044,
        "new decoder origin");
}
