#pragma once

#include <map>
#include <string>
#include <string_view>

struct NavOptimizerParams;
struct TransformOptimizerParams;
struct CompositionOptimizerParams;

// Parses and applies optimizer parameter overrides from Lua.
class OptimizerParamOverrides {
public:
  OptimizerParamOverrides() = default;

  // Malformed or wrong-scope lines warn at parse time and no-op at apply time.
  // `shared:` is restricted to OptimizerParamsBase fields.
  static OptimizerParamOverrides parse(std::string_view encoded);

  void applyTo(NavOptimizerParams& params) const;
  void applyTo(TransformOptimizerParams& params) const;
  void applyTo(CompositionOptimizerParams& params) const;

  bool empty() const {
    return shared_.empty() && nav_.empty() && transform_.empty() && composition_.empty();
  }

  // TODO review if this is the best design pattern
  template <class Params>
  static Params resolved(const OptimizerParamOverrides* overrides) {
    Params params;
    if (overrides) overrides->applyTo(params);
    return params;
  }

private:
  using ScopeMap = std::map<std::string, std::string, std::less<>>;
  ScopeMap shared_;
  ScopeMap nav_;
  ScopeMap transform_;
  ScopeMap composition_;
};
