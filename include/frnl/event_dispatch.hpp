#pragma once
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace frnl {
// Synchronous host-independent counterpart of FrnlTS state-scoped events.
// Async dispatch is intentionally delegated to the embedding host.
class EventDispatcher {
 public:
  using Handler = std::function<std::string(const std::string&)>;
  using Listener = std::function<void(const std::string&, const std::string&,
                                      const std::string&, const std::string&)>;
  void register_component(const std::string& id, const std::string& state) {
    require_name(id, "component"); require_name(state, "state");
    if (!components_.emplace(id, Component{state, {}, {}}).second)
      throw std::invalid_argument("duplicate component: " + id);
  }
  void define_event(const std::string& id, const std::string& state,
                    const std::string& event, const std::string& function_id) {
    auto& component = component_at(id);
    require_name(state, "state"); require_name(event, "event");
    require_name(function_id, "function");
    if (!component.events[state].emplace(event, function_id).second)
      throw std::invalid_argument("duplicate event definition: " + event);
  }
  void register_handler(const std::string& id, const std::string& function_id,
                        Handler handler) {
    auto& component = component_at(id);
    require_name(function_id, "function");
    if (!handler) throw std::invalid_argument("empty event handler");
    if (!component.handlers.emplace(function_id, std::move(handler)).second)
      throw std::invalid_argument("duplicate event handler: " + function_id);
  }
  void set_state(const std::string& id, const std::string& state) {
    auto& component = component_at(id);
    if (component.events.find(state) == component.events.end())
      throw std::invalid_argument("undefined component state: " + state);
    component.state = state;
  }
  void add_listener(Listener listener) {
    if (!listener) throw std::invalid_argument("empty event listener");
    listeners_.push_back(std::move(listener));
  }
  std::string invoke(const std::string& id, const std::string& event,
                     const std::string& payload) {
    auto& component = component_at(id);
    auto state = component.events.find(component.state);
    if (state == component.events.end())
      throw std::invalid_argument("undefined component state: " + component.state);
    auto definition = state->second.find(event);
    if (definition == state->second.end())
      throw std::invalid_argument("event not defined in active state: " + event);
    auto handler = component.handlers.find(definition->second);
    if (handler == component.handlers.end())
      throw std::invalid_argument("missing handler: " + definition->second);
    const auto result = handler->second(payload);
    // Snapshot before callbacks: a listener may register another listener.
    // Mutating listeners_ while iterating it would invalidate iterators.
    const auto listeners = listeners_;
    for (const auto& listener : listeners) listener(id, event, payload, result);
    return result;
  }
 private:
  struct Component {
    std::string state;
    std::map<std::string, std::map<std::string, std::string>> events;
    std::map<std::string, Handler> handlers;
  };
  static void require_name(const std::string& value, const char* kind) {
    if (value.empty()) throw std::invalid_argument(std::string("empty ") + kind);
  }
  Component& component_at(const std::string& id) {
    auto it = components_.find(id);
    if (it == components_.end()) throw std::invalid_argument("unknown component: " + id);
    return it->second;
  }
  std::map<std::string, Component> components_;
  std::vector<Listener> listeners_;
};
} // namespace frnl
