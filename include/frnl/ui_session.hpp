#pragma once
// Host-driven Frnl UI session: no socket, timer or network dependency.
// Host owns transport/authentication, tick scheduling and endpoint cleanup.
#include "frontend.hpp"
#include <any>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace frnl {
struct UiCommand {
  std::string id, revision, component_id, control_id;
  std::any payload;
};
struct UiCompletion { std::string id; bool succeeded = false; };
using UiSessionTransport = std::function<std::optional<UiCommand>(
    const FrontendSnapshot&, const std::string&, const std::optional<UiCompletion>&)>;
using UiActionPolicy = std::function<bool(const ComponentSnapshot&, const std::string&)>;

inline void fingerprint_piece(std::string& text, const std::string& part) {
  text += std::to_string(part.size()) + ":" + part;
}
inline std::string fingerprint_value(const Value& value) {
  return std::visit([](const auto& data) -> std::string {
    using T = std::decay_t<decltype(data)>;
    if constexpr (std::is_same_v<T, std::monostate>) return "null";
    else if constexpr (std::is_same_v<T, bool>) return data ? "true" : "false";
    else if constexpr (std::is_same_v<T, std::string>) return "str:" + data;
    else { std::ostringstream stream; stream << std::setprecision(17) << data; return stream.str(); }
  }, value);
}
inline std::string snapshot_fingerprint(const FrontendSnapshot& snapshot) {
  std::string fingerprint;
  fingerprint_piece(fingerprint, snapshot.application_id);
  fingerprint_piece(fingerprint, snapshot.route);
  for (const auto& component : snapshot.components) {
    fingerprint_piece(fingerprint, component.id);
    fingerprint_piece(fingerprint, component.type);
    fingerprint_piece(fingerprint, component.parent);
    fingerprint_piece(fingerprint, component.state);
    fingerprint_piece(fingerprint, component.presentation.role);
    fingerprint_piece(fingerprint, component.presentation.label);
    fingerprint_piece(fingerprint, component.presentation.disabled ? "disabled" : "enabled");
    fingerprint_piece(fingerprint, component.presentation.loading ? "loading" : "ready");
    fingerprint_piece(fingerprint, component.realized.mounted ? "mounted" : "unmounted");
    fingerprint_piece(fingerprint, component.realized.visible ? "visible" : "hidden");
    for (const auto& prop : component.properties) {
      fingerprint_piece(fingerprint, prop.first);
      fingerprint_piece(fingerprint, fingerprint_value(prop.second));
    }
    for (const auto& prop : component.visual_parameters) {
      fingerprint_piece(fingerprint, prop.first);
      fingerprint_piece(fingerprint, fingerprint_value(prop.second));
    }
    for (const auto& control : component.controls) {
      fingerprint_piece(fingerprint, control.first);
      fingerprint_piece(fingerprint, control.second ? "1" : "0");
    }
    if (component.realized.geometry) {
      const auto& g = *component.realized.geometry;
      std::ostringstream s;
      s << std::setprecision(17) << g.x << "," << g.y << "," << g.width << "," << g.height;
      fingerprint_piece(fingerprint, s.str());
    }
  }
  return fingerprint;
}

class UiSession {
 public:
  UiSession(FrontendApi& api, UiSessionTransport transport, UiActionPolicy allowed = {},
            std::function<void(std::exception_ptr)> on_error = {})
      : api_(api), transport_(std::move(transport)),
        allowed_(std::move(allowed)), on_error_(std::move(on_error)) {
    if (!transport_) throw std::invalid_argument("Frnl UI session requires a host transport");
  }
  UiSession(const UiSession&) = delete;
  UiSession& operator=(const UiSession&) = delete;
  void stop() noexcept { stopped_ = true; }
  bool stopped() const noexcept { return stopped_; }
  const std::string& revision() const noexcept { return revision_; }
  const std::optional<UiCompletion>& pending_completion() const noexcept { return completion_; }

  // Called by the host clock. Sends no implicit network requests and owns no timer.
  bool tick() {
    if (stopped_) return false;
    try {
      const auto snapshot = api_.inspect();
      const auto before = snapshot_fingerprint(snapshot);
      if (before != fingerprint_) {
        fingerprint_ = before;
        revision_ = "revision-" + std::to_string(++sequence_);
      }
      const auto command = transport_(snapshot, revision_, completion_);
      completion_.reset();
      if (!command) return false;
      completion_ = UiCompletion{command->id, false};
      if (command->id.empty() || command->revision != revision_ ||
          snapshot_fingerprint(api_.inspect()) != before)
        return true;
      for (const auto& component : snapshot.components) {
        if (component.id != command->component_id) continue;
        const auto control = component.controls.find(command->control_id);
        const bool event = control != component.controls.end() && control->second &&
            command->control_id != "setState" &&
            command->control_id != "setVisualParameter" &&
            command->control_id != "applyTheme";
        if (!event || !component.rendered || !component.realized.mounted ||
            !component.realized.visible || component.presentation.disabled ||
            component.presentation.loading || !allowed_ ||
            !allowed_(component, command->control_id))
          return true;
        // Authorization policies and host measurements may themselves mutate UI state.
        // Fence once more before invoking any externally supplied action.
        if (snapshot_fingerprint(api_.inspect()) != before) return true;
        try {
          (void)api_.invoke(command->component_id, command->control_id, command->payload);
          completion_->succeeded = true;
        } catch (...) {
          if (on_error_) on_error_(std::current_exception());
        }
        return true;
      }
      return true;
    } catch (...) {
      if (on_error_) on_error_(std::current_exception());
      else throw;
      return false;
    }
  }
 private:
  FrontendApi& api_;
  UiSessionTransport transport_;
  UiActionPolicy allowed_;
  std::function<void(std::exception_ptr)> on_error_;
  bool stopped_ = false;
  std::uint64_t sequence_ = 0;
  std::string revision_, fingerprint_;
  std::optional<UiCompletion> completion_;
};
} // namespace frnl
