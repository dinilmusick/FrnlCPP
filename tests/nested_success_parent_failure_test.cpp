#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// A nested successful event is observable even if its parent handler fails.
// The failed parent event must not notify listeners or roll back nested effects.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("component", "ready");
  dispatcher.define_event("component", "ready", "inner", "inner_handler");
  dispatcher.define_event("component", "ready", "outer", "outer_handler");
  dispatcher.register_handler("component", "inner_handler",
                              [](const std::string& value) { return value + ":inner"; });
  bool fail = true;
  dispatcher.register_handler("component", "outer_handler",
                              [&](const std::string& value) {
    const auto nested = dispatcher.invoke("component", "inner", value);
    if (fail) {
      fail = false;
      throw std::runtime_error("outer failed after nested success");
    }
    return nested + ":outer";
  });
  std::vector<std::string> observed;
  dispatcher.add_listener([&](const std::string&, const std::string& event,
                              const std::string&, const std::string& result) {
    observed.push_back(event + "=" + result);
  });
  try {
    (void)dispatcher.invoke("component", "outer", "first");
    return 1;
  } catch (const std::runtime_error&) {
    // Expected: the outer handler failed after its inner event completed.
  }
  if (observed != std::vector<std::string>{"inner=first:inner"}) return 2;
  if (dispatcher.invoke("component", "outer", "second") != "second:inner:outer") return 3;
  if (observed != std::vector<std::string>{
      "inner=first:inner", "inner=second:inner", "outer=second:inner:outer"}) return 4;
  return 0;
}
