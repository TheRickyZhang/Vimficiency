#include "OptimizerParamOverrides.h"

#include <charconv>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <fast_float/fast_float.h>

#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"

namespace {

std::optional<int> parseInt(std::string_view s) {
  int v = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
  return v;
}

std::optional<double> parseDouble(std::string_view s) {
  double v = 0.0;
  auto [ptr, ec] = fast_float::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
  return v;
}

std::optional<bool> parseBool(std::string_view s) {
  if (s == "1" || s == "true") return true;
  if (s == "0" || s == "false") return false;
  return std::nullopt;
}

// Whitespace is NOT stripped — Lua emits clean "key=value".
std::optional<std::pair<std::string_view, std::string_view>>
splitOnce(std::string_view s, char c) {
  const auto pos = s.find(c);
  if (pos == std::string_view::npos) return std::nullopt;
  return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
}

const std::set<std::string_view, std::less<>>& knownKeys() {
  static const std::set<std::string_view, std::less<>> keys = {
#define VF_KEY(type, name, withName, def, parser) #name,
      OPTIMIZER_BASE_FIELDS(VF_KEY)
      MOTION_CLASS_FIELDS(VF_KEY)
      NAV_OWN_FIELDS(VF_KEY)
      TRANSFORM_FIELDS(VF_KEY)
      COMPOSITION_OWN_FIELDS(VF_KEY)
#undef VF_KEY
  };
  return keys;
}

const std::set<std::string_view, std::less<>>& baseKeys() {
  static const std::set<std::string_view, std::less<>> keys = {
#define VF_KEY(type, name, withName, def, parser) #name,
      OPTIMIZER_BASE_FIELDS(VF_KEY)
#undef VF_KEY
  };
  return keys;
}

const std::set<std::string_view, std::less<>>& navKeys() {
  static const std::set<std::string_view, std::less<>> keys = {
#define VF_KEY(type, name, withName, def, parser) #name,
      OPTIMIZER_BASE_FIELDS(VF_KEY)
      NAV_FIELDS(VF_KEY)
#undef VF_KEY
  };
  return keys;
}

const std::set<std::string_view, std::less<>>& transformKeys() {
  static const std::set<std::string_view, std::less<>> keys = {
#define VF_KEY(type, name, withName, def, parser) #name,
      OPTIMIZER_BASE_FIELDS(VF_KEY)
      TRANSFORM_FIELDS(VF_KEY)
#undef VF_KEY
  };
  return keys;
}

const std::set<std::string_view, std::less<>>& compositionKeys() {
  static const std::set<std::string_view, std::less<>> keys = {
#define VF_KEY(type, name, withName, def, parser) #name,
      OPTIMIZER_BASE_FIELDS(VF_KEY)
      COMPOSITION_FIELDS(VF_KEY)
#undef VF_KEY
  };
  return keys;
}

std::string_view canonicalKey(std::string_view scope, std::string_view key) {
  if (scope == "composition" && key == "treeDiffOpenPenalty") {
    return "diffOpenPenalty";
  }
  if (scope == "composition" && key == "treeMoveDeleteScale") {
    return "moveDeleteScale";
  }
  return key;
}

bool keyAllowedInScope(std::string_view scope, std::string_view key) {
  if (scope == "shared") return baseKeys().contains(key);
  if (scope == "nav") return navKeys().contains(key);
  if (scope == "transform") return transformKeys().contains(key);
  if (scope == "composition") return compositionKeys().contains(key);
  return false;
}

bool valueValidForKey(std::string_view key, std::string_view value) {
#define VF_MATCH(type, name, withName, def, parser)                  \
  if (key == #name) return parser(value).has_value();
  OPTIMIZER_BASE_FIELDS(VF_MATCH)
  MOTION_CLASS_FIELDS(VF_MATCH)
  NAV_OWN_FIELDS(VF_MATCH)
  TRANSFORM_FIELDS(VF_MATCH)
  COMPOSITION_OWN_FIELDS(VF_MATCH)
#undef VF_MATCH
  return false;
}

bool applyBaseField(OptimizerParamsBase& p,
                    std::string_view key,
                    std::string_view value) {
#define VF_MATCH(type, name, withName, def, parser)                  \
  if (key == #name) {                                                \
    if (auto v = parser(value)) p.name = *v;                         \
    return true;                                                     \
  }
  OPTIMIZER_BASE_FIELDS(VF_MATCH)
#undef VF_MATCH
  return false;
}

bool applyNavField(NavOptimizerParams& p,
                   std::string_view key,
                   std::string_view value) {
  if (applyBaseField(p, key, value)) return true;
#define VF_MATCH(type, name, withName, def, parser)                  \
  if (key == #name) {                                                \
    if (auto v = parser(value)) p.name = *v;                         \
    return true;                                                     \
  }
  NAV_FIELDS(VF_MATCH)
#undef VF_MATCH
  return false;
}

bool applyTransformField(TransformOptimizerParams& p,
                         std::string_view key,
                         std::string_view value) {
  if (applyBaseField(p, key, value)) return true;
#define VF_MATCH(type, name, withName, def, parser)                  \
  if (key == #name) {                                                \
    if (auto v = parser(value)) p.name = *v;                         \
    return true;                                                     \
  }
  TRANSFORM_FIELDS(VF_MATCH)
#undef VF_MATCH
  return false;
}

bool applyCompositionField(CompositionOptimizerParams& p,
                           std::string_view key,
                           std::string_view value) {
  if (applyBaseField(p, key, value)) return true;
#define VF_MATCH(type, name, withName, def, parser)                  \
  if (key == #name) {                                                \
    if (auto v = parser(value)) p.name = *v;                         \
    return true;                                                     \
  }
  COMPOSITION_FIELDS(VF_MATCH)
#undef VF_MATCH
  return false;
}

template <typename ApplyFn>
void applyMap(const std::map<std::string, std::string, std::less<>>& kv, ApplyFn&& apply) {
  for (const auto& [k, v] : kv) apply(k, v);
}

std::string scopedKey(std::string_view scope, std::string_view key) {
  return std::string(scope) + ":" + std::string(key);
}

}  // namespace

class OptimizerParamOverridesParser {
public:
  static OptimizerParamOverridesParseResult parse(std::string_view encoded) {
    OptimizerParamOverrides out;
    OptimizerParamOverrideErrors errors;
    int lineNumber = 0;
    while (!encoded.empty()) {
      const auto eolPos = encoded.find('\n');
      const auto line = encoded.substr(0, eolPos);
      encoded.remove_prefix(
          eolPos == std::string_view::npos ? encoded.size() : eolPos + 1);
      lineNumber++;
      if (line.empty()) continue;

      const auto scopeSplit = splitOnce(line, ':');
      if (!scopeSplit) {
        errors.push_back(
            "line " + std::to_string(lineNumber) +
            ": expected '<scope>:<key>=<value>'");
        continue;
      }
      const auto& [scope, rest] = *scopeSplit;

      const auto kvSplit = splitOnce(rest, '=');
      if (!kvSplit) {
        errors.push_back(
            "line " + std::to_string(lineNumber) +
            ": expected '<scope>:<key>=<value>'");
        continue;
      }
      const auto& [key, value] = *kvSplit;
      const std::string_view normalizedKey = canonicalKey(scope, key);

      OptimizerParamOverrides::ScopeMap* target = nullptr;
      if (scope == "shared") target = &out.shared_;
      else if (scope == "nav") target = &out.nav_;
      else if (scope == "transform") target = &out.transform_;
      else if (scope == "composition") target = &out.composition_;
      else {
        errors.push_back(
            "line " + std::to_string(lineNumber) +
            ": unknown scope '" + std::string(scope) +
            "', expected shared/nav/transform/composition");
        continue;
      }

      if (!knownKeys().contains(normalizedKey)) {
        errors.push_back(
            "line " + std::to_string(lineNumber) +
            ": unknown optimizer key '" + scopedKey(scope, key) + "'");
        continue;
      } else if (!keyAllowedInScope(scope, normalizedKey)) {
        errors.push_back(
            "line " + std::to_string(lineNumber) +
            ": optimizer key '" + scopedKey(scope, key) +
            "' is not valid in that scope");
        continue;
      }

      if (!valueValidForKey(normalizedKey, value)) {
        errors.push_back(
            "line " + std::to_string(lineNumber) +
            ": invalid value for optimizer key '" + scopedKey(scope, key) +
            "': '" + std::string(value) + "'");
        continue;
      }

      target->insert_or_assign(std::string(normalizedKey), std::string(value));
    }
    if (!errors.empty()) return std::unexpected(std::move(errors));
    return out;
  }
};

OptimizerParamOverridesParseResult parseOptimizerParamOverrides(std::string_view encoded) {
  return OptimizerParamOverridesParser::parse(encoded);
}

std::string formatOptimizerParamOverrideErrors(
    const OptimizerParamOverrideErrors& errors) {
  std::string out = "invalid optimizer overrides";
  for (const auto& error : errors) {
    out += "\n";
    out += error;
  }
  return out;
}

void OptimizerParamOverrides::applyTo(NavOptimizerParams& p) const {
  applyMap(shared_, [&](auto& k, auto& v) { applyBaseField(p, k, v); });
  applyMap(nav_, [&](auto& k, auto& v) { applyNavField(p, k, v); });
  p.validate();
}

void OptimizerParamOverrides::applyTo(TransformOptimizerParams& p) const {
  applyMap(shared_, [&](auto& k, auto& v) { applyBaseField(p, k, v); });
  applyMap(transform_, [&](auto& k, auto& v) { applyTransformField(p, k, v); });
  p.validate();
}

void OptimizerParamOverrides::applyTo(CompositionOptimizerParams& p) const {
  applyMap(shared_, [&](auto& k, auto& v) { applyBaseField(p, k, v); });
  applyMap(composition_, [&](auto& k, auto& v) { applyCompositionField(p, k, v); });
  p.validate();
}
