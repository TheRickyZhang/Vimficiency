#pragma once

#include "Utils/Debug.h"

#include <memory>
#include <unordered_map>
#include <utility>

namespace VF::LuaExports {

// Owns pending async jobs keyed by an integer id handed back to Lua. Like
// ViewRegistry, only ever touched on the main (Lua) thread; workers touch only
// their own job object. `JobT` supplies the worker/cancel/done machinery.
template <typename JobT>
class JobRegistry {
 public:
  int create(std::unique_ptr<JobT> job) {
    const int id = ++next_id_;
    jobs_.emplace(id, std::move(job));
    return id;
  }
  JobT& get(int id) {
    auto it = jobs_.find(id);
    CHECK(it != jobs_.end(), "job_id from Lua not present in registry");
    return *it->second;
  }
  std::unique_ptr<JobT> take(int id) {
    auto it = jobs_.find(id);
    if (it == jobs_.end()) return nullptr;
    auto job = std::move(it->second);
    jobs_.erase(it);
    return job;
  }

 private:
  std::unordered_map<int, std::unique_ptr<JobT>> jobs_;
  int next_id_ = 0;
};

}  // namespace VF::LuaExports
