#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// A listener may subscribe another listener and synchronously dispatch a nested
// event. The nested dispatch sees the new subscription; the outer snapshot does not.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("component", "ready");
  dispatcher.define_event("component", "ready", "outer", "outer_handler");
  dispatcher.define_event("component", "ready", "inner", "inner_handler");
  dispatcher.register_handler("component", "outer_handler",
                              [](const std::string& p) { return p + ":outer"; });
  dispatcher.register_handler("component", "inner_handler",
                              [](const std::string& p) { return p + ":inner"; });
  std::vector<std::string> observed;
  bool nested = false;
  dispatcher.add_listener([&](const std::string&, const std::string& event,
                              const std::string&, const std::string&) {
    observed.push_back("first:" + event);
    if (event == "outer" && !nested) {
      nested = true;
      dispatcher.add_listener([&](const std::string&, const std::string& nested_event,
                                  const std::string&, const std::string&) {
        observed.push_back("late:" + nested_event);
      });
      if (dispatcher.invoke("component", "inner", "payload") != "payload:inner")
        throw std::runtime_error("nested dispatch result mismatch");
    }
  });
  if (dispatcher.invoke("component", "outer", "payload") != "payload:outer")
    return 1;
  if (observed != std::vector<std::string>{"first:outer", "first:inner", "late:inner"})
    return 2;
  if (dispatcher.invoke("component", "outer", "again") != "again:outer")
    return 3;
  if (observed != std::vector<std::string>{"first:outer", "first:inner", "late:inner",
                                           "first:outer", "late:outer"})
    return 4;
  return 0;
}
