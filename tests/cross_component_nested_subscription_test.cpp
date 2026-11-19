#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// A handler may subscribe a listener and dispatch into another component.
// The nested call sees the new subscription; the outer call retains its snapshot.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("outer", "ready");
  dispatcher.register_component("inner", "ready");
  dispatcher.define_event("outer", "ready", "fire", "outer_handler");
  dispatcher.define_event("inner", "ready", "fire", "inner_handler");
  dispatcher.register_handler("inner", "inner_handler",
                              [](const std::string& value) { return value + ":inner"; });
  std::vector<std::string> notifications;
  dispatcher.add_listener([&](const std::string& id, const std::string&,
                               const std::string&, const std::string&) {
    notifications.push_back("existing:" + id);
  });
  dispatcher.register_handler("outer", "outer_handler", [&](const std::string& value) {
    dispatcher.add_listener([&](const std::string& id, const std::string&,
                                 const std::string&, const std::string&) {
      notifications.push_back("new:" + id);
    });
    if (dispatcher.invoke("inner", "fire", value) != value + ":inner")
      throw std::runtime_error("nested result mismatch");
    return value + ":outer";
  });
  if (dispatcher.invoke("outer", "fire", "payload") != "payload:outer") return 1;
  if (notifications != std::vector<std::string>{
          "existing:inner", "new:inner", "existing:outer"}) return 2;
  return 0;
}
