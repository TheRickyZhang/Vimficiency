#include "OptimizerParamOverrides.h"

#include <charconv>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Utils/Debug.h"

namespace {

std::optional<int> parseInt(std::string_view s) {
  int v = 0;
  auto [_, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{}) return std::nullopt;
  return v;
}

std::optional<double> parseDouble(std::string_view s) {
  double v = 0.0;
  auto [_, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{}) return std::nullopt;
  return v;
}

std::optional<bool> parseBool(std::string_view s) {
  if (s == "1" || s == "true") return true;
  if (s == "0" || s == "false") return false;
  return std::nullopt;
}

// Split `s` at the first byte equal to `c`; returns nullopt if `c` not
// found. Tolerant: trailing/leading whitespace is NOT stripped — Lua
// emits clean "key=value" so we don't pay for it.
std::optional<std::pair<std::string_view, std::string_view>>
splitOnce(std::string_view s, char c) {
  const auto pos = s.find(c);
  if (pos == std::string_view::npos) return std::nullopt;
  return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
}

// Union of every key recognized by any of the three apply* functions
// below. Used to distinguish typos (warn) from scope mismatches
// (silent fall-through, by design). Keep this in lockstep with the
// branches in applyBaseField / applyNavField / applyTransformField /
// applyCompositionField — drift here means typos pass unnoticed.
const std::set<std::string_view, std::less<>>& knownKeys() {
  static const std::set<std::string_view, std::less<>> kKeys = {
      // Base
      "maxResults", "maxNodesPopped", "exploreFactor",
      "effortWeight", "distanceWeight",
      "minPrefixCount", "maxPrefixCount",
      // Nav-only
      "fMotionThreshold", "useDirectionalPruning", "maxResultsPerEndPos",
      // Transform-only
      "maxResultsPerStartPos",
      // Composition-only
      "navPaddingAbove", "navPaddingBelow",
      "overshootPenalty", "transformMaxResultsPerStartPos",
  };
  return kKeys;
}

// Apply a single (key, value) to OptimizerParamsBase fields. Returns
// true if the key matched a known base field (caller can then fall
// through to derived-only fields).
bool applyBaseField(OptimizerParamsBase& base,
                    std::string_view key,
                    std::string_view value) {
  if (key == "maxResults") {
    if (auto v = parseInt(value)) base.maxResults = *v;
  } else if (key == "maxNodesPopped") {
    if (auto v = parseInt(value)) base.maxNodesPopped = *v;
  } else if (key == "exploreFactor") {
    if (auto v = parseDouble(value)) base.exploreFactor = *v;
  } else if (key == "effortWeight") {
    if (auto v = parseDouble(value)) base.effortWeight = *v;
  } else if (key == "distanceWeight") {
    if (auto v = parseDouble(value)) base.distanceWeight = *v;
  } else if (key == "minPrefixCount") {
    if (auto v = parseInt(value)) base.setMinCountRepeat(*v);
  } else if (key == "maxPrefixCount") {
    if (auto v = parseInt(value)) base.setMaxCountRepeat(*v);
  } else {
    return false;
  }
  return true;
}

void applyNavField(NavOptimizerParams& p,
                   std::string_view key,
                   std::string_view value) {
  if (applyBaseField(p, key, value)) return;
  if (key == "fMotionThreshold") {
    if (auto v = parseInt(value)) p.fMotionThreshold = *v;
  } else if (key == "useDirectionalPruning") {
    if (auto v = parseBool(value)) p.useDirectionalPruning = *v;
  } else if (key == "maxResultsPerEndPos") {
    if (auto v = parseInt(value)) p.maxResultsPerEndPos = *v;
  }
  // Unknown key — silently ignored.
}

void applyTransformField(TransformOptimizerParams& p,
                         std::string_view key,
                         std::string_view value) {
  if (applyBaseField(p, key, value)) return;
  if (key == "maxResultsPerStartPos") {
    if (auto v = parseInt(value)) p.maxResultsPerStartPos = *v;
  }
}

void applyCompositionField(CompositionOptimizerParams& p,
                           std::string_view key,
                           std::string_view value) {
  if (applyBaseField(p, key, value)) return;
  if (key == "fMotionThreshold") {
    if (auto v = parseInt(value)) p.fMotionThreshold = *v;
  } else if (key == "useDirectionalPruning") {
    if (auto v = parseBool(value)) p.useDirectionalPruning = *v;
  } else if (key == "navPaddingAbove") {
    if (auto v = parseInt(value)) p.navPaddingAbove = *v;
  } else if (key == "navPaddingBelow") {
    if (auto v = parseInt(value)) p.navPaddingBelow = *v;
  } else if (key == "overshootPenalty") {
    if (auto v = parseDouble(value)) p.overshootPenalty = *v;
  } else if (key == "transformMaxResultsPerStartPos") {
    if (auto v = parseInt(value)) p.transformMaxResultsPerStartPos = *v;
  }
}

template <typename ApplyFn>
void applyMap(const std::map<std::string, std::string, std::less<>>& kv, ApplyFn&& apply) {
  for (const auto& [k, v] : kv) apply(k, v);
}

}  // namespace

OptimizerParamOverrides OptimizerParamOverrides::parse(std::string_view encoded) {
  OptimizerParamOverrides out;
  while (!encoded.empty()) {
    const auto eolPos = encoded.find('\n');
    const auto line = encoded.substr(0, eolPos);
    encoded.remove_prefix(eolPos == std::string_view::npos ? encoded.size() : eolPos + 1);
    if (line.empty()) continue;

    const auto scopeSplit = splitOnce(line, ':');
    if (!scopeSplit) {
      warning("optimizer override: malformed line, expected '<scope>:<key>=<value>': '", line, "'");
      continue;
    }
    const auto& [scope, rest] = *scopeSplit;

    const auto kvSplit = splitOnce(rest, '=');
    if (!kvSplit) {
      warning("optimizer override: malformed line, expected 'key=value': '", line, "'");
      continue;
    }
    const auto& [key, value] = *kvSplit;

    ScopeMap* target = nullptr;
    if (scope == "shared") target = &out.shared_;
    else if (scope == "nav") target = &out.nav_;
    else if (scope == "transform") target = &out.transform_;
    else if (scope == "composition") target = &out.composition_;
    else {
      warning("optimizer override: unknown scope '", scope,
              "', expected one of shared/nav/transform/composition");
      continue;
    }

    if (!knownKeys().contains(key)) {
      warning("optimizer override: unknown key '", key, "' (scope ", scope, ")");
      // Still record it — the apply* dispatch will silently skip it,
      // and recording keeps round-trip equality if anyone re-encodes.
    }

    target->insert_or_assign(std::string(key), std::string(value));
  }
  return out;
}

void OptimizerParamOverrides::applyTo(NavOptimizerParams& p) const {
  applyMap(shared_, [&](auto& k, auto& v) { applyNavField(p, k, v); });
  applyMap(nav_, [&](auto& k, auto& v) { applyNavField(p, k, v); });
}

void OptimizerParamOverrides::applyTo(TransformOptimizerParams& p) const {
  applyMap(shared_, [&](auto& k, auto& v) { applyTransformField(p, k, v); });
  applyMap(transform_, [&](auto& k, auto& v) { applyTransformField(p, k, v); });
}

void OptimizerParamOverrides::applyTo(CompositionOptimizerParams& p) const {
  applyMap(shared_, [&](auto& k, auto& v) { applyCompositionField(p, k, v); });
  applyMap(composition_, [&](auto& k, auto& v) { applyCompositionField(p, k, v); });
}
