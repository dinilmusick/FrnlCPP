#include "frnl/event_dispatch.hpp"
#include <string>
#include <vector>

// The outer dispatch snapshots listeners before its handler runs. A listener
// installed by that handler is visible to a nested invocation, not the outer one.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("panel", "ready");
  dispatcher.define_event("panel", "ready", "outer", "outer_handler");
  dispatcher.define_event("panel", "ready", "inner", "inner_handler");
  std::vector<std::string> observed;
  dispatcher.add_listener([&](const std::string&, const std::string& event,
                              const std::string&, const std::string&) {
    observed.push_back("early:" + event);
  });
  dispatcher.register_handler("panel", "inner_handler",
                              [](const std::string& payload) { return payload + ":inner"; });
  dispatcher.register_handler("panel", "outer_handler",
                              [&](const std::string& payload) {
    dispatcher.add_listener([&](const std::string&, const std::string& event,
                                const std::string&, const std::string&) {
      observed.push_back("late:" + event);
    });
    if (dispatcher.invoke("panel", "inner", payload) != payload + ":inner")
      throw std::runtime_error("nested result mismatch");
    return payload + ":outer";
  });
  if (dispatcher.invoke("panel", "outer", "x") != "x:outer") return 1;
  if (observed != std::vector<std::string>{"early:inner", "late:inner", "early:outer"})
    return 2;
  if (dispatcher.invoke("panel", "inner", "y") != "y:inner") return 3;
  if (observed != std::vector<std::string>{"early:inner", "late:inner", "early:outer",
                                           "early:inner", "late:inner"}) return 4;
  return 0;
}
