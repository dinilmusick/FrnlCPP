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
  struct EventContext {
    const std::string component_id;
    const std::string state; // Snapshot of the state at dispatch entry.
    std::function<void(const std::string&)> set_state;
  };
  using ContextHandler = std::function<std::string(const std::string&, const EventContext&)>;
  using Listener = std::function<void(const std::string&, const std::string&,
                                      const std::string&, const std::string&)>;
  void register_component(const std::string& id, const std::string& state) {
    require_name(id, "component"); require_name(state, "state");
    if (!components_.emplace(id, Component{state, {}, {}, {}}).second)
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
    if (component.context_handlers.count(function_id) || !component.handlers.emplace(function_id, std::move(handler)).second)
      throw std::invalid_argument("duplicate event handler: " + function_id);
  }
  void register_context_handler(const std::string& id, const std::string& function_id,
                                ContextHandler handler) {
    auto& component = component_at(id);
    require_name(function_id, "function");
    if (!handler) throw std::invalid_argument("empty event handler");
    if (component.handlers.count(function_id) ||
        !component.context_handlers.emplace(function_id, std::move(handler)).second)
      throw std::invalid_argument("duplicate event handler: " + function_id);
  }
  void set_state(const std::string& id, const std::string& state) {
    auto& component = component_at(id);
    if (component.events.find(state) == component.events.end())
      throw std::invalid_argument("undefined component state: " + state);
    component.state = state;
  }
  // Subscription handles support explicit host/UI lifecycle disposal.
  // A removed listener remains in any already captured dispatch snapshot.
  using SubscriptionId = std::size_t;
  SubscriptionId subscribe(Listener listener) {
    if (!listener) throw std::invalid_argument("empty event listener");
    const auto id = next_subscription_id_++;
    listeners_.emplace(id, std::move(listener));
    return id;
  }
  bool unsubscribe(SubscriptionId id) { return listeners_.erase(id) != 0; }
  void add_listener(Listener listener) { (void)subscribe(std::move(listener)); }
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
    auto context_handler = component.context_handlers.find(definition->second);
    if (handler == component.handlers.end() && context_handler == component.context_handlers.end())
      throw std::invalid_argument("missing handler: " + definition->second);
    const EventContext context{id, component.state,
      [this, id](const std::string& next) { set_state(id, next); }};
    // Capture listeners before executing user code. Both handlers and listeners
    // may subscribe new listeners; subscriptions begin with the next event.
    const auto listeners = listeners_;
    const auto result = context_handler != component.context_handlers.end()
      ? context_handler->second(payload, context) : handler->second(payload);
    for (const auto& entry : listeners) entry.second(id, event, payload, result);
    return result;
  }
 private:
  struct Component {
    std::string state;
    std::map<std::string, std::map<std::string, std::string>> events;
    std::map<std::string, Handler> handlers;
    std::map<std::string, ContextHandler> context_handlers;
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
  std::map<SubscriptionId, Listener> listeners_;
  SubscriptionId next_subscription_id_ = 1;
};
} // namespace frnl
