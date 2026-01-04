#pragma once

struct ImpliedExclusions {
  // Use snake_case here since it is better for listing motions
  bool exclude_G{};
  bool exclude_gg{};
  ImpliedExclusions() = default;
  ImpliedExclusions(bool G, bool gg) : exclude_G(G), exclude_gg(gg) {}
};
