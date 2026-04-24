#pragma once

#include <string>

namespace Explore {

// Shared error leg for Explore session layers. Returned by handler helpers
// via std::expected and by Session actions wrapped in Outcome.
// State is guaranteed unchanged when an action produces a Rejected.
struct Rejected {
  std::string reason;
};

}  // namespace Explore
