#pragma once

#include "Optimizer/SearchControl.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

namespace VF::LuaExports {

// Common machinery for an off-main-thread optimizer search. Holds a
// SearchControl (cancel flag + optional deadline), a done flag the main thread
// polls, and the owning jthread. Both the Explore (View trace) and suggest
// (analyze result) async paths derive from this; they differ only in their
// owned inputs/output and the run body passed to `spawn`.
//
// Invariant: VimOptions::shiftwidth (the one runtime-mutable VimCore global the
// search reads) must not be reconfigured while a worker is live. The session
// sets it once at setup; single-flight on the suggest side keeps at most one
// suggest worker alive.
class AsyncJob {
 public:
  explicit AsyncJob(int deadlineMs) {
    if (deadlineMs > 0) {
      control_.hasDeadline = true;
      control_.deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(deadlineMs);
    }
  }

  bool ready() const { return done_.load(std::memory_order_acquire); }
  void cancel() { control_.cancelled.store(true, std::memory_order_relaxed); }

 protected:
  // Spawn the worker. `run` executes on the new thread; `done_` is published
  // with release ordering once it returns, so a `ready()` acquire-load that
  // observes true also sees the fully-written output. Call only after the job
  // sits at a stable heap address (the run body captures `this`).
  template <typename Fn>
  void spawn(Fn&& run) {
    worker_ = std::jthread([this, run = std::forward<Fn>(run)]() mutable {
      run();
      done_.store(true, std::memory_order_release);
    });
  }

  const SearchControl* control() const { return &control_; }

 private:
  SearchControl control_;
  std::atomic<bool> done_{false};
  std::jthread worker_;
};

}  // namespace VF::LuaExports
