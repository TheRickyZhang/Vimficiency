#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>

#include "LuaExports/AsyncJob.h"
#include "Optimizer/SearchControl.h"

using VF::LuaExports::AsyncJob;

namespace {

TEST(AsyncJobTest, ReadyPublishesTheState) {
  auto job = std::make_unique<AsyncJob<int>>(
      0, 0, [](int& value, const SearchControl*) { value = 42; });
  while (!job->ready()) std::this_thread::yield();
  EXPECT_EQ(job->state(), 42);
}

// Records, at destruction, whether the run body had finished with it.
struct SpinState {
  std::atomic<bool>* finishedAtDestruction;
  bool finished = false;
  ~SpinState() { finishedAtDestruction->store(finished); }
};

// The cancel path drops a job whose worker is still running. The worker must
// be joined before the state it writes is destroyed — the member order in
// AsyncJob is what guarantees it.
TEST(AsyncJobTest, DroppingACancelledJobJoinsBeforeItsStateDies) {
  std::atomic<bool> finishedAtDestruction{false};
  {
    auto job = std::make_unique<AsyncJob<SpinState>>(
        SpinState{&finishedAtDestruction}, 0,
        [](SpinState& s, const SearchControl* control) {
          while (!control->cancelRequested()) std::this_thread::yield();
          s.finished = true;
        });
    job->cancel();
  }
  EXPECT_TRUE(finishedAtDestruction.load());
}

}  // namespace
