#pragma once
// Headless frontend inspection and controls. Renderer measurements remain host-owned.
#include "presentation.hpp"
#include <any>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace frnl {
struct VisualChange {
  std::string parameter;
  Value value;
};
struct ComponentSnapshot {
  std::string id, type, parent, state;
  std::vector<std::string> path, children, events;
  Properties properties, visual_parameters;
  Presentation presentation;
  RealizedState realized;
  // Names in the map are the only callable runtime controls.
  std::map<std::string, bool> controls;
  bool rendered = true;
};
struct FrontendSnapshot {
  unsigned api_version = 1;
  std::string application_id, route;
  std::vector<ComponentSnapshot> components;
  std::vector<std::string> roots;
  std::size_t component_count = 0;
};
struct FrontendListeners {
  using Listener = std::function<void(const FrontendSnapshot&)>;
  std::map<std::size_t, Listener> handlers;
  std::size_t next = 1;
  bool disposed = false;
};
class FrontendSubscription {
 public:
  FrontendSubscription() = default;
  FrontendSubscription(std::weak_ptr<FrontendListeners> state, std::size_t id)
      : state_(std::move(state)), id_(id) {}
  FrontendSubscription(const FrontendSubscription&) = delete;
  FrontendSubscription& operator=(const FrontendSubscription&) = delete;
  FrontendSubscription(FrontendSubscription&& other) noexcept
      : state_(std::move(other.state_)), id_(other.id_) { other.id_ = 0; }
  FrontendSubscription& operator=(FrontendSubscription&& other) noexcept {
    if (this != &other) {
      reset(); state_ = std::move(other.state_); id_ = other.id_; other.id_ = 0;
    }
    return *this;
  }
  ~FrontendSubscription() { reset(); }
  void reset() noexcept {
    if (auto state = state_.lock()) state->handlers.erase(id_);
    state_.reset(); id_ = 0;
  }
 private:
  std::weak_ptr<FrontendListeners> state_;
  std::size_t id_ = 0;
};

class FrontendApi {
 public:
  FrontendApi(SemanticRuntime& runtime, std::string application = {},
              std::string route = {}, MeasurementPort measurement = {})
      : runtime_(runtime), application_(std::move(application)),
        route_(std::move(route)), measurement_(std::move(measurement)),
        listeners_(std::make_shared<FrontendListeners>()) {
    const auto weak = std::weak_ptr<FrontendListeners>(listeners_);
    // The callback does not touch a destroyed facade because reset() unsubscribes.
    updates_ = runtime_.on_update([this, weak](const Update&) {
      auto state = weak.lock();
      if (!state || state->disposed) return;
      const FrontendSnapshot snapshot = inspect();
      const auto copied = state->handlers;
      for (const auto& item : copied) item.second(snapshot);
    });
  }
  FrontendApi(const FrontendApi&) = delete;
  FrontendApi& operator=(const FrontendApi&) = delete;
  ~FrontendApi() { dispose(); }

  ComponentSnapshot inspect_component(const std::string& id) const {
    ensure_open();
    const auto& definition = runtime_.definition(id);
    ComponentSnapshot item;
    item.id = id; item.type = definition.name;
    item.state = runtime_.current_state(id);
    item.parent = runtime_.parent_id(id);
    item.path = runtime_.component_path(id);
    item.children = runtime_.children_of(id);
    item.events = runtime_.event_names(id);
    item.properties = runtime_.properties(id);
    item.visual_parameters = runtime_.visual_parameters(id);
    item.presentation = resolve_presentation(definition, item.properties, item.state);
    item.realized = measurement_
        ? measurement_(id).value_or(RealizedState{})
        : RealizedState{true, true, {}, {}, {}};
    validate_realized(item.realized);
    for (const auto& event : item.events)
      item.controls[event] = runtime_.event_available(id, event);
    item.controls["setState"] = true;
    item.controls["setVisualParameter"] = true;
    item.controls["applyTheme"] = runtime_.has_any_theme();
    return item;
  }
  FrontendSnapshot inspect() const {
    ensure_open();
    FrontendSnapshot result;
    result.application_id = application_;
    result.route = route_;
    const auto ids = runtime_.component_ids();
    result.component_count = ids.size();
    for (const auto& id : ids) {
      auto snapshot = inspect_component(id);
      if (snapshot.parent.empty()) result.roots.push_back(id);
      result.components.push_back(std::move(snapshot));
    }
    return result;
  }
  std::any invoke(const std::string& id, const std::string& control,
                  const std::any& payload = {}) {
    ensure_open();
    const auto snap = inspect_component(id);
    const auto it = snap.controls.find(control);
    if (it == snap.controls.end()) throw Fault("CONTROL_NOT_FOUND", "Unknown Frnl control: " + control);
    if (!it->second) throw Fault("CONTROL_UNAVAILABLE", "Frnl control not available: " + control);
    if (control == "setState") {
      runtime_.set_state(id, std::any_cast<const std::string&>(payload));
      return {};
    }
    if (control == "setVisualParameter") {
      const auto& change = std::any_cast<const VisualChange&>(payload);
      runtime_.set_visual_override(id, change.parameter, change.value);
      return {};
    }
    if (control == "applyTheme") {
      runtime_.apply_theme(id, std::any_cast<const std::string&>(payload));
      return {};
    }
    return runtime_.invoke(id, control, payload);
  }
  FrontendSubscription subscribe(FrontendListeners::Listener listener) {
    ensure_open();
    if (!listener) throw std::invalid_argument("empty frontend listener");
    const auto id = listeners_->next++;
    listeners_->handlers.emplace(id, std::move(listener));
    try { listeners_->handlers.at(id)(inspect()); }
    catch (...) { listeners_->handlers.erase(id); throw; }
    return FrontendSubscription{listeners_, id};
  }
  void dispose() noexcept {
    if (disposed_) return;
    disposed_ = true;
    listeners_->disposed = true;
    updates_.reset();
    listeners_->handlers.clear();
  }
  bool disposed() const noexcept { return disposed_; }

 private:
  void ensure_open() const {
    if (disposed_) throw Fault("FRONTEND_API_DISPOSED", "Frnl frontend inspection disposed");
  }
  SemanticRuntime& runtime_;
  std::string application_, route_;
  MeasurementPort measurement_;
  std::shared_ptr<FrontendListeners> listeners_;
  Subscription updates_;
  bool disposed_ = false;
};
} // namespace frnl
