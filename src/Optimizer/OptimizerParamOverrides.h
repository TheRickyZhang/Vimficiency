#pragma once

#include <map>
#include <string>
#include <string_view>

struct NavOptimizerParams;
struct TransformOptimizerParams;
struct CompositionOptimizerParams;

// Parses and applies key=value optimizer parameter overrides arriving
// from Lua over the FFI boundary. Wire format: one override per line
//
//   <scope>:<key>=<value>
//
// where <scope> ∈ {shared, nav, transform, composition}. Empty input
// means "use C++ defaults". Unknown keys for a given target struct are
// silently ignored — callers can hand the same shared payload to every
// optimizer without filtering, and per-optimizer keys harmlessly fall
// through when applied to the wrong target.
//
// `shared:` writes to fields living on OptimizerParamsBase (and any
// shadowed copies on the derived struct, see applyTo for details).
class OptimizerParamOverrides {
public:
  OptimizerParamOverrides() = default;

  // Parse encoded overrides. Malformed lines are skipped (not an error)
  // so partial/tolerant decoding survives wire-format drift; unknown
  // keys get caught later at apply time with the same silent-skip rule.
  static OptimizerParamOverrides parse(std::string_view encoded);

  void applyTo(NavOptimizerParams& params) const;
  void applyTo(TransformOptimizerParams& params) const;
  void applyTo(CompositionOptimizerParams& params) const;

  bool empty() const {
    return shared_.empty() && nav_.empty() && transform_.empty() && composition_.empty();
  }

private:
  using ScopeMap = std::map<std::string, std::string, std::less<>>;
  ScopeMap shared_;
  ScopeMap nav_;
  ScopeMap transform_;
  ScopeMap composition_;
};
