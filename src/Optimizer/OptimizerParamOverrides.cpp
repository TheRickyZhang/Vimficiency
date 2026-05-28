#include "OptimizerParamOverrides.h"

#include <cerrno>
#include <charconv>
#include <cstdlib>
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
  // Apple's libc++ deletes std::from_chars for floating point (P0067 still
  // unimplemented as of Xcode 16). strtod needs NUL-terminated input.
  std::string buf(s);
  char* end = nullptr;
  errno = 0;
  double v = std::strtod(buf.c_str(), &end);
  if (end != buf.c_str() + buf.size() || errno != 0) return std::nullopt;
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

bool keyAllowedInScope(std::string_view scope, std::string_view key) {
  if (scope == "shared") return baseKeys().contains(key);
  if (scope == "nav") return navKeys().contains(key);
  if (scope == "transform") return transformKeys().contains(key);
  if (scope == "composition") return compositionKeys().contains(key);
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
      // Still recorded so re-encoding is a round-trip.
    } else if (!keyAllowedInScope(scope, key)) {
      warning("optimizer override: key '", key,
              "' is not valid in scope '", scope, "'");
    }

    target->insert_or_assign(std::string(key), std::string(value));
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
