#pragma once

#include "Optimizer/SearchControl.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

namespace VF::LuaExports {

// An off-main-thread optimizer search over state the job owns. `run(state,
// control)` executes on the worker; the main thread polls `ready()` and reads
// `state()` only once it is true, or calls `cancel()` and drops the job.
//
// Lifetime: the destructor joins `worker_` before any member is torn down. Dropping a job whose worker is still running (the cancel
// path) blocks until the search observes the flag and returns, so the worker
// can never outlive what it reads or writes. The join is bounded by the
// search's polling granularity: per pop, and between composition setup phases.
//
// Invariant: VimOptions::shiftwidth (the one runtime-mutable VimCore global the
// search reads) must not be reconfigured while a worker is live. The session
// sets it once at setup; single-flight on the suggest side keeps at most one
// suggest worker alive.
template <typename State>
class AsyncJob {
 public:
  // Starts the worker immediately. Construct in place (make_unique): the run
  // body captures `this`.
  template <typename Fn>
  AsyncJob(State state, int deadlineMs, Fn run) : state_(std::move(state)) {
    if (deadlineMs > 0) {
      control_.hasDeadline = true;
      control_.deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(deadlineMs);
    }
    worker_ = std::thread([this, run = std::move(run)]() mutable {
      run(state_, &control_);
      done_.store(true, std::memory_order_release);
    });
  }

  // std::thread rather than std::jthread: Apple's libc++ does not ship jthread.
  ~AsyncJob() {
    if (worker_.joinable()) worker_.join();
  }
  AsyncJob(const AsyncJob&) = delete;
  AsyncJob& operator=(const AsyncJob&) = delete;

  // True once `run` has returned; the release/acquire pair means an observer
  // of true also sees the fully written state.
  bool ready() const { return done_.load(std::memory_order_acquire); }
  void cancel() { control_.cancelled.store(true, std::memory_order_relaxed); }
  // Only meaningful after `ready()`.
  State& state() { return state_; }

 private:
  SearchControl control_;
  std::atomic<bool> done_{false};
  State state_;
  std::thread worker_;
};

}  // namespace VF::LuaExports
