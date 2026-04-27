#pragma once

#include "Explore/Explore.h"
#include "Utils/Debug.h"

#include <unordered_map>
#include <utility>

namespace LuaExports {

// Owns all live Explore views. Lua holds opaque integer handles handed out
// by `create`; lookup misses are programmer bugs (stale handle, use-after-
// destroy) and abort the process via CHECK.
class ViewRegistry {
 public:
  template <typename... Args>
  int create(Args&&... args) {
    const int id = ++next_id_;
    views_.try_emplace(id, std::forward<Args>(args)...);
    return id;
  }

  bool destroy(int id) { return views_.erase(id) > 0; }

  Explore::View& get(int id) {
    auto it = views_.find(id);
    CHECK(it != views_.end(), "view_id from Lua not present in registry");
    return it->second;
  }

 private:
  std::unordered_map<int, Explore::View> views_;
  int next_id_ = 0;
};

}  // namespace LuaExports
