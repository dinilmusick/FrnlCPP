#pragma once
// Pure renderer-independent presentation/accessibility contracts.
#include "semantic_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace frnl {
struct Presentation {
  std::string role;
  std::string label;
  bool disabled = false;
  bool active = false;
  bool selected = false;
  bool loading = false;
};
inline std::optional<std::string> textual(const Properties& props, const std::string& key) {
  const auto it = props.find(key);
  if (it == props.end()) return std::nullopt;
  const auto text = std::get_if<std::string>(&it->second);
  if (!text) return std::nullopt;
  const auto start = text->find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return std::nullopt;
  const auto end = text->find_last_not_of(" \t\n\r");
  return text->substr(start, end - start + 1);
}
inline std::optional<bool> flag(const Properties& props, const std::string& key) {
  const auto it = props.find(key);
  if (it == props.end()) return std::nullopt;
  if (const auto b = std::get_if<bool>(&it->second)) return *b;
  if (const auto s = std::get_if<std::string>(&it->second)) {
    if (*s == "true") return true;
    if (*s == "false") return false;
  }
  return std::nullopt;
}
inline bool first_flag(bool fallback, std::initializer_list<std::optional<bool>> values) {
  for (const auto& value : values) if (value) return *value;
  return fallback;
}
inline Presentation resolve_presentation(const ComponentDefinition& definition,
                                        const Properties& props, const std::string& state) {
  Presentation p;
  p.role = textual(props, "role").value_or(definition.presentation_role);
  p.label = textual(props, "aria-label")
      .value_or(textual(props, "label").value_or(definition.presentation_label));
  p.disabled = first_flag(state == "disabled", {flag(props, "aria-disabled"), flag(props, "disabled")});
  p.active = first_flag(state == "active", {flag(props, "active"), flag(props, "isActive")});
  p.selected = first_flag(state == "selected", {flag(props, "aria-selected"), flag(props, "selected"), flag(props, "isSelected")});
  p.loading = first_flag(state == "loading", {flag(props, "aria-busy"), flag(props, "loading"), flag(props, "isLoading")});
  return p;
}
struct AccessibilityContract {
  std::string role;
  bool name_required = false;
  bool focusable = false;
  std::vector<std::string> keyboard;
  std::vector<std::string> required_attributes;
  std::vector<std::string> provided_semantics;
};
struct AccessibilityIssue {
  std::string code;
  std::string message;
};
inline std::vector<AccessibilityIssue> validate_accessibility(
    const AccessibilityContract& contract, const Properties& attrs) {
  std::vector<AccessibilityIssue> problems;
  if (contract.name_required &&
      !textual(attrs, "aria-label") && !textual(attrs, "aria-labelledby"))
    problems.push_back({"ACCESSIBLE_NAME_REQUIRED", "Missing aria-label or aria-labelledby"});
  for (const auto& key : contract.required_attributes) {
    const auto it = attrs.find(key);
    if (it == attrs.end() || std::holds_alternative<std::monostate>(it->second) ||
        (std::holds_alternative<std::string>(it->second) &&
         std::get<std::string>(it->second).empty()))
      problems.push_back({"REQUIRED_ATTRIBUTE_MISSING", "Missing attribute: " + key});
  }
  if (!contract.role.empty() && textual(attrs, "role") != std::optional<std::string>(contract.role))
    problems.push_back({"ROLE_MISMATCH", "Unexpected semantic role"});
  if (contract.focusable) {
    const auto hidden = flag(attrs, "aria-hidden").value_or(false);
    bool excluded = false;
    const auto it = attrs.find("tabIndex");
    if (it != attrs.end()) {
      if (const auto n = std::get_if<std::int64_t>(&it->second)) excluded = *n == -1;
    }
    if (hidden || excluded) problems.push_back({"FOCUS_RESTRICTED", "Focusable element suppressed"});
  }
  return problems;
}

struct Geometry { double x = 0, y = 0, width = 0, height = 0; };
struct RealizedState {
  bool mounted = false;
  bool visible = false;
  std::optional<bool> in_viewport;
  std::optional<Geometry> geometry;
  std::map<std::string, std::string> layout;
};
using MeasurementPort = std::function<std::optional<RealizedState>(const std::string&)>;
inline void validate_realized(const RealizedState& value) {
  if (!value.geometry) return;
  const auto& g = *value.geometry;
  if (!std::isfinite(g.x) || !std::isfinite(g.y) ||
      !std::isfinite(g.width) || !std::isfinite(g.height) ||
      g.width < 0.0 || g.height < 0.0)
    throw std::invalid_argument("invalid native renderer geometry");
}
} // namespace frnl
