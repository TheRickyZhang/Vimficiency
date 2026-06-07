#pragma once

#include <atomic>
#include <chrono>

// Cooperative stop signal for a long-running optimizer search run on a worker
// thread. The owning thread sets `cancelled` (teardown) or arms a `deadline`
// (churn bound); the search polls `stopRequested` in its main loop and returns
// best-so-far when it fires. The wall-clock read is gated on `totalPops` so a
// deadline costs one clock read per 1024 pops rather than one per pop.
struct SearchControl {
  std::atomic<bool> cancelled{false};
  bool hasDeadline = false;
  std::chrono::steady_clock::time_point deadline;

  bool stopRequested(int totalPops) const {
    if (cancelled.load(std::memory_order_relaxed)) return true;
    if (hasDeadline && (totalPops & 1023) == 0 &&
        std::chrono::steady_clock::now() >= deadline) {
      return true;
    }
    return false;
  }
};
