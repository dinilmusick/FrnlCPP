#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// A successful cross-component nested event remains observable if its caller
// fails; subscriptions added during the caller are excluded from its snapshot.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("caller", "ready");
  dispatcher.register_component("callee", "ready");
  dispatcher.define_event("caller", "ready", "outer", "outer_handler");
  dispatcher.define_event("callee", "ready", "inner", "inner_handler");
  dispatcher.register_handler("callee", "inner_handler",
      [](const std::string& value) { return value + ":inner"; });
  std::vector<std::string> observed;
  dispatcher.add_listener([&](const std::string& component,
                              const std::string& event,
                              const std::string&, const std::string&) {
    observed.push_back("early:" + component + ":" + event);
  });
  bool fail = true;
  dispatcher.register_handler("caller", "outer_handler",
      [&](const std::string& value) {
        if (fail) {
          dispatcher.add_listener([&](const std::string& component,
                                      const std::string& event,
                                      const std::string&, const std::string&) {
            observed.push_back("late:" + component + ":" + event);
          });
        }
        const auto result = dispatcher.invoke("callee", "inner", value);
        if (fail) {
          fail = false;
          throw std::runtime_error("parent failed");
        }
        return result + ":outer";
      });
  try {
    (void)dispatcher.invoke("caller", "outer", "first");
    return 1;
  } catch (const std::runtime_error&) {
  }
  if (observed != std::vector<std::string>{
      "early:callee:inner", "late:callee:inner"}) return 2;
  if (dispatcher.invoke("caller", "outer", "second") !=
      "second:inner:outer") return 3;
  if (observed != std::vector<std::string>{
      "early:callee:inner", "late:callee:inner",
      "early:callee:inner", "late:callee:inner",
      "early:caller:outer", "late:caller:outer"}) return 4;
  return 0;
}
