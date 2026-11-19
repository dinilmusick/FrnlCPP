#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// A handler may change its own active state and synchronously invoke the
// newly active state's event. The outer event must retain its original
// listener snapshot and failure must not roll back the state transition.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("unit", "ready");
  dispatcher.define_event("unit", "ready", "advance", "advance_handler");
  dispatcher.define_event("unit", "active", "confirm", "confirm_handler");
  dispatcher.register_handler("unit", "confirm_handler",
      [](const std::string& value) { return value + ":confirmed"; });
  std::vector<std::string> observed;
  dispatcher.add_listener([&](const std::string&, const std::string& event,
                              const std::string&, const std::string&) {
    observed.push_back("original:" + event);
  });
  dispatcher.register_handler("unit", "advance_handler",
      [&](const std::string& value) -> std::string {
        dispatcher.set_state("unit", "active");
        dispatcher.add_listener([&](const std::string&, const std::string& event,
                                    const std::string&, const std::string&) {
          observed.push_back("added:" + event);
        });
        if (dispatcher.invoke("unit", "confirm", value) != value + ":confirmed")
          throw std::logic_error("nested result mismatch");
        throw std::runtime_error("outer failed after state transition");
      });
  try {
    (void)dispatcher.invoke("unit", "advance", "first");
    return 1;
  } catch (const std::runtime_error&) {
  }
  if (observed != std::vector<std::string>{"original:confirm", "added:confirm"})
    return 2;
  try {
    (void)dispatcher.invoke("unit", "advance", "second");
    return 3;
  } catch (const std::invalid_argument&) {
  }
  if (dispatcher.invoke("unit", "confirm", "second") != "second:confirmed")
    return 4;
  if (observed != std::vector<std::string>{
      "original:confirm", "added:confirm",
      "original:confirm", "added:confirm"}) return 5;
  return 0;
}
