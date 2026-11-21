#pragma once
// FrnlCPP renderer-neutral semantic runtime (C++17).
// Explicit host adapters; no dependency on Qt, SDL, React, Skipo or Floks.
#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace frnl {

using Value = std::variant<std::monostate, bool, std::int64_t, double, std::string>;
using Properties = std::map<std::string, Value>;
using Data = std::map<std::string, std::any>;

enum class Phase { attack, sustain, release };
struct PhaseDurations {
  double attack_seconds = 0.0;
  double release_seconds = 0.0;
};
struct StateDefinition {
  std::map<std::string, std::string> events;
  Properties visual_parameters;
  PhaseDurations phases;
};
struct ComponentDefinition {
  std::string name;
  std::map<std::string, StateDefinition> states;
  std::string presentation_role;
  std::string presentation_label;
};
struct Update {
  std::string component_id;
  std::string kind;
  std::string state;
  Phase phase = Phase::sustain;
  Properties effective_visuals;
};
struct EventNotice {
  std::string component_id;
  std::string event;
  std::string state_at_entry;
  std::any payload;
  std::any result;
};
using Theme = std::map<std::string, std::map<std::string, Properties>>;
struct HostPorts {
  // The embedding host owns loop, thread-affinity, clock and persistence.
  std::function<void(std::function<void()>)> schedule_on_ui;
  std::function<double()> monotonic_seconds;
  std::function<void(const std::string&, const std::any&)> persist;
  std::function<std::any(const std::string&)> restore;
};

struct ObserverState {
  using UpdateListener = std::function<void(const Update&)>;
  using EventListener = std::function<void(const EventNotice&)>;
  std::map<std::size_t, UpdateListener> updates;
  std::map<std::size_t, EventListener> events;
  std::size_t next = 1;
};

// Token destruction never dereferences a destroyed runtime.
class Subscription {
 public:
  Subscription() = default;
  Subscription(std::weak_ptr<ObserverState> state, std::size_t id, bool event)
      : state_(std::move(state)), id_(id), event_(event) {}
  Subscription(const Subscription&) = delete;
  Subscription& operator=(const Subscription&) = delete;
  Subscription(Subscription&& other) noexcept { move_from(other); }
  Subscription& operator=(Subscription&& other) noexcept {
    if (this != &other) { reset(); move_from(other); }
    return *this;
  }
  ~Subscription() { reset(); }
  void reset() noexcept {
    if (auto state = state_.lock()) {
      if (event_) state->events.erase(id_);
      else state->updates.erase(id_);
    }
    state_.reset();
    id_ = 0;
  }
 private:
  void move_from(Subscription& other) noexcept {
    state_ = std::move(other.state_); id_ = other.id_; event_ = other.event_;
    other.id_ = 0;
  }
  std::weak_ptr<ObserverState> state_;
  std::size_t id_ = 0;
  bool event_ = false;
};

class SemanticRuntime {
 public:
  using Handler = std::function<std::any(SemanticRuntime&, const std::string&, const std::any&)>;
  using LibraryFunction = std::function<std::any(const std::vector<std::any>&)>;
  struct FlowStep {
    std::string id;
    std::vector<std::string> dependencies;
    std::function<void(SemanticRuntime&)> action;
  };

  SemanticRuntime() : observers_(std::make_shared<ObserverState>()) {}
  SemanticRuntime(const SemanticRuntime&) = delete;
  SemanticRuntime& operator=(const SemanticRuntime&) = delete;
  SemanticRuntime(SemanticRuntime&&) = delete;
  SemanticRuntime& operator=(SemanticRuntime&&) = delete;

  void register_component(std::string id, ComponentDefinition definition,
                          std::string initial_state, std::string parent = {}) {
    require(id, "component ID"); require(definition.name, "component definition");
    if (nodes_.count(id)) throw std::invalid_argument("duplicate component: " + id);
    if (!parent.empty() && !nodes_.count(parent))
      throw std::invalid_argument("unknown parent: " + parent);
    if (parent == id) throw std::invalid_argument("component cannot parent itself");
    if (definition.states.empty() || !definition.states.count(initial_state))
      throw std::invalid_argument("undefined initial state: " + initial_state);
    for (const auto& state : definition.states) {
      require(state.first, "state");
      const auto durations = state.second.phases;
      if (!valid_duration(durations.attack_seconds) ||
          !valid_duration(durations.release_seconds))
        throw std::invalid_argument("invalid phase duration");
      for (const auto& event : state.second.events) {
        require(event.first, "event"); require(event.second, "function ID");
      }
    }
    const Phase initial_phase =
        definition.states.at(initial_state).phases.attack_seconds > 0.0
            ? Phase::attack : Phase::sustain;
    nodes_.emplace(std::move(id), Node{std::move(parent), std::move(definition),
                                     std::move(initial_state), initial_phase,
                                     0.0, {}, {}, {}, {}, {}});
  }

  bool has_component(const std::string& id) const noexcept {
    return nodes_.find(id) != nodes_.end();
  }
  void remove_component(const std::string& id) {
    (void)node(id);
    for (const auto& child : nodes_)
      if (child.second.parent == id)
        throw std::logic_error("remove children before parent: " + id);
    const auto old = node(id);
    nodes_.erase(id);
    publish(Update{id, "disposed", old.state, old.phase, {}});
  }
  std::vector<std::string> component_path(const std::string& id) const {
    std::vector<std::string> path;
    std::set<std::string> seen;
    std::string current = id;
    while (!current.empty()) {
      if (!seen.insert(current).second)
        throw std::logic_error("component ancestry cycle");
      const auto& entry = node(current);
      path.push_back(current);
      current = entry.parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
  }
  std::string component_at_path(const std::vector<std::string>& path) const {
    if (path.empty() || component_path(path.back()) != path)
      throw std::invalid_argument("unknown or forged component path");
    return path.back();
  }
  const std::string& current_state(const std::string& id) const { return node(id).state; }
  Phase current_phase(const std::string& id) const { return node(id).phase; }
  double phase_elapsed(const std::string& id) const { return node(id).elapsed; }

  void set_state(const std::string& id, const std::string& next) {
    auto& n = node(id);
    if (!n.definition.states.count(next))
      throw std::invalid_argument("undefined state: " + next);
    if (n.pending) { n.pending = next; return; }
    if (n.state == next) return;
    const double release = n.definition.states.at(n.state).phases.release_seconds;
    if (release > 0.0) {
      n.pending = next; n.phase = Phase::release; n.elapsed = 0.0;
      notify(id, "phase");
    } else activate(id, next);
  }

  // Host-driven deterministic phase clock. Sustain curves use a one-second loop.
  void advance(const std::string& id, double seconds) {
    if (!valid_duration(seconds)) throw std::invalid_argument("invalid clock delta");
    double remaining = seconds;
    for (int transitions = 0; transitions < 5; ++transitions) {
      auto& n = node(id);
      if (n.phase == Phase::sustain) {
        n.elapsed = std::fmod(n.elapsed + remaining, 1.0);
        notify(id, "phase");
        return;
      }
      const double duration = n.phase == Phase::attack
          ? n.definition.states.at(n.state).phases.attack_seconds
          : n.definition.states.at(n.state).phases.release_seconds;
      const double step = std::min(remaining, std::max(0.0, duration - n.elapsed));
      n.elapsed += step;
      remaining -= step;
      if (n.elapsed < duration) { notify(id, "phase"); return; }
      if (n.phase == Phase::attack) {
        n.phase = Phase::sustain; n.elapsed = 0.0;
        notify(id, "phase");
      } else {
        const std::string next = n.pending.value_or(n.state);
        activate(id, next);
      }
      if (remaining <= 0.0) return;
    }
    throw std::logic_error("invalid phase progression");
  }
  void advance_all(double seconds) {
    std::vector<std::string> ids;
    for (const auto& entry : nodes_) ids.push_back(entry.first);
    for (const auto& id : ids) if (has_component(id)) advance(id, seconds);
  }

  void register_theme(std::string name, Theme theme) {
    require(name, "theme");
    themes_.insert_or_assign(std::move(name), std::move(theme));
  }
  void apply_theme(const std::string& id, const std::string& name) {
    auto& n = node(id);
    if (!themes_.count(name)) throw std::invalid_argument("unknown theme: " + name);
    for (const auto& state : themes_.at(name)[n.definition.name])
      if (!n.definition.states.count(state.first))
        throw std::invalid_argument("theme references undefined state: " + state.first);
    n.theme = name;
    notify(id, "theme");
  }
  void clear_theme(const std::string& id) {
    node(id).theme.reset(); notify(id, "theme");
  }
  Properties visual_parameters(const std::string& id) const {
    const auto& n = node(id);
    Properties result = n.definition.states.at(n.state).visual_parameters;
    std::string cursor = id;
    std::optional<std::string> inherited;
    while (!cursor.empty()) {
      const auto& ancestor = node(cursor);
      if (ancestor.theme) { inherited = ancestor.theme; break; }
      cursor = ancestor.parent;
    }
    if (inherited) {
      const auto theme = themes_.find(*inherited);
      if (theme != themes_.end()) {
        const auto comp = theme->second.find(n.definition.name);
        if (comp != theme->second.end()) {
          const auto state = comp->second.find(n.state);
          if (state != comp->second.end())
            for (const auto& entry : state->second) result[entry.first] = entry.second;
        }
      }
    }
    for (const auto& entry : n.overrides) result[entry.first] = entry.second;
    return result;
  }
  void set_visual_override(const std::string& id, std::string key, Value value) {
    require(key, "visual property");
    node(id).overrides.insert_or_assign(std::move(key), std::move(value));
    notify(id, "visual");
  }
  void set_property(const std::string& id, std::string key, Value value) {
    require(key, "property");
    node(id).properties.insert_or_assign(std::move(key), std::move(value));
    notify(id, "property");
  }
  const Properties& properties(const std::string& id) const { return node(id).properties; }
  void set_component_data(const std::string& id, std::string key, std::any value) {
    require(key, "data key");
    node(id).local.insert_or_assign(std::move(key), std::move(value));
    notify(id, "data");
  }
  const std::any& component_data(const std::string& id, const std::string& key) const {
    return node(id).local.at(key);
  }
  void set_page_data(std::string key, std::any value) {
    require(key, "page data key");
    page_data_.insert_or_assign(std::move(key), std::move(value));
  }
  const std::any& page_data(const std::string& key) const { return page_data_.at(key); }

  void register_handler(std::string id, Handler handler) {
    require(id, "handler");
    if (!handler || !handlers_.emplace(std::move(id), std::move(handler)).second)
      throw std::invalid_argument("invalid or duplicate handler");
  }
  std::any invoke(const std::string& id, const std::string& event,
                  const std::any& payload = {}) {
    const auto& n = node(id);
    const std::string state = n.state;
    const auto& events = n.definition.states.at(state).events;
    const auto found = events.find(event);
    if (found == events.end()) throw std::invalid_argument("event not defined in active state: " + event);
    const auto handler = handlers_.find(found->second);
    if (handler == handlers_.end())
      throw std::invalid_argument("unregistered function: " + found->second);
    const Handler callback = handler->second;
    const auto snapshot = observers_->events; // nested dispatch snapshot isolation
    const std::any result = callback(*this, id, payload);
    const EventNotice notice{id, event, state, payload, result};
    for (const auto& listener : snapshot) listener.second(notice);
    return result;
  }
  std::any semantic_input(const std::vector<std::string>& path,
                          const std::string& event, const std::any& payload = {}) {
    return invoke(component_at_path(path), event, payload);
  }
  Subscription on_update(ObserverState::UpdateListener listener) {
    if (!listener) throw std::invalid_argument("empty update listener");
    const auto id = observers_->next++;
    observers_->updates.emplace(id, std::move(listener));
    return Subscription{observers_, id, false};
  }
  Subscription on_event(ObserverState::EventListener listener) {
    if (!listener) throw std::invalid_argument("empty event listener");
    const auto id = observers_->next++;
    observers_->events.emplace(id, std::move(listener));
    return Subscription{observers_, id, true};
  }

  void register_library(std::string id, LibraryFunction fn) {
    require(id, "library");
    if (!fn || !libraries_.emplace(std::move(id), std::move(fn)).second)
      throw std::invalid_argument("invalid or duplicate library function");
  }
  std::any call_library(const std::string& id,
                        const std::vector<std::any>& args = {}) const {
    return libraries_.at(id)(args);
  }
  void register_flow(std::string name, std::vector<FlowStep> steps) {
    require(name, "flow");
    if (flows_.count(name)) throw std::invalid_argument("duplicate flow: " + name);
    std::map<std::string, std::size_t> positions;
    for (std::size_t i = 0; i < steps.size(); ++i) {
      require(steps[i].id, "flow step");
      if (!steps[i].action || !positions.emplace(steps[i].id, i).second)
        throw std::invalid_argument("invalid or duplicate flow step");
    }
    std::map<std::string, int> color;
    std::vector<std::size_t> order;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
      if (!positions.count(id)) throw std::invalid_argument("unknown flow dependency: " + id);
      if (color[id] == 1) throw std::invalid_argument("cyclic flow dependency: " + id);
      if (color[id] == 2) return;
      color[id] = 1;
      const auto& step = steps[positions.at(id)];
      for (const auto& dep : step.dependencies) visit(dep);
      color[id] = 2;
      order.push_back(positions.at(id));
    };
    for (const auto& step : steps) visit(step.id);
    flows_.emplace(std::move(name), Flow{std::move(steps), std::move(order)});
  }
  void execute_flow(const std::string& name) {
    const Flow copy = flows_.at(name); // stable under nested registrations
    for (const auto position : copy.order) {
      const auto& step = copy.steps.at(position);
      try { step.action(*this); }
      catch (...) {
        std::throw_with_nested(std::runtime_error("Frnl flow step failed: " + name + "/" + step.id));
      }
    }
  }

  std::vector<std::string> diagnostics() const {
    std::vector<std::string> errors;
    for (const auto& component : nodes_)
      for (const auto& state : component.second.definition.states)
        for (const auto& event : state.second.events)
          if (!handlers_.count(event.second))
            errors.push_back(component.first + "/" + state.first + "/" +
                             event.first + ": missing handler " + event.second);
    return errors;
  }
  void set_host_ports(HostPorts ports) { ports_ = std::move(ports); }
  void schedule_on_ui(std::function<void()> fn) const {
    if (!fn || !ports_.schedule_on_ui) throw std::logic_error("UI scheduler not installed");
    ports_.schedule_on_ui(std::move(fn));
  }
  double monotonic_time() const {
    if (!ports_.monotonic_seconds) throw std::logic_error("clock not installed");
    return ports_.monotonic_seconds();
  }
  void persist(const std::string& key, const std::any& value) const {
    if (!ports_.persist) throw std::logic_error("persistence not installed");
    ports_.persist(key, value);
  }
  std::any restore(const std::string& key) const {
    if (!ports_.restore) throw std::logic_error("persistence not installed");
    return ports_.restore(key);
  }

 private:
  struct Node {
    std::string parent;
    ComponentDefinition definition;
    std::string state;
    Phase phase = Phase::sustain;
    double elapsed = 0.0;
    std::optional<std::string> pending;
    Properties properties;
    Properties overrides;
    Data local;
    std::optional<std::string> theme;
  };
  struct Flow { std::vector<FlowStep> steps; std::vector<std::size_t> order; };
  static void require(const std::string& name, const char* type) {
    if (name.empty()) throw std::invalid_argument(std::string("empty ") + type);
  }
  static bool valid_duration(double seconds) {
    return std::isfinite(seconds) && seconds >= 0.0;
  }
  Node& node(const std::string& id) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) throw std::invalid_argument("unknown component: " + id);
    return found->second;
  }
  const Node& node(const std::string& id) const {
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) throw std::invalid_argument("unknown component: " + id);
    return found->second;
  }
  void activate(const std::string& id, const std::string& next) {
    auto& n = node(id);
    n.state = next;
    n.pending.reset();
    n.elapsed = 0.0;
    n.phase = n.definition.states.at(n.state).phases.attack_seconds > 0.0
        ? Phase::attack : Phase::sustain;
    notify(id, "state");
  }
  void notify(const std::string& id, const std::string& kind) {
    const auto& n = node(id);
    publish(Update{id, kind, n.state, n.phase, visual_parameters(id)});
  }
  void publish(const Update& update) {
    const auto snapshot = observers_->updates;
    for (const auto& observer : snapshot) observer.second(update);
  }
  std::map<std::string, Node> nodes_;
  Data page_data_;
  std::map<std::string, Theme> themes_;
  std::map<std::string, Handler> handlers_;
  std::map<std::string, LibraryFunction> libraries_;
  std::map<std::string, Flow> flows_;
  std::shared_ptr<ObserverState> observers_;
  HostPorts ports_;
};

} // namespace frnl
