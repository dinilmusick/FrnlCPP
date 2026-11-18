#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// A listener exception must propagate, without corrupting future dispatch.
// Subscriptions added by a failing listener apply only to subsequent events.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("c", "ready");
  dispatcher.define_event("c", "ready", "fire", "handler");
  dispatcher.register_handler("c", "handler", [](const std::string& payload) {
    return payload + ":done";
  });
  std::vector<std::string> seen;
  bool fail_once = true;
  dispatcher.add_listener([&](const std::string&, const std::string&,
                              const std::string&, const std::string&) {
    seen.push_back("first");
    if (fail_once) {
      fail_once = false;
      dispatcher.add_listener([&](const std::string&, const std::string&,
                                  const std::string&, const std::string&) {
        seen.push_back("late");
      });
      throw std::runtime_error("listener failure");
    }
  });
  dispatcher.add_listener([&](const std::string&, const std::string&,
                              const std::string&, const std::string&) {
    seen.push_back("second");
  });
  bool caught = false;
  try { (void)dispatcher.invoke("c", "fire", "p"); }
  catch (const std::runtime_error&) { caught = true; }
  if (!caught || seen != std::vector<std::string>{"first"}) return 1;
  if (dispatcher.invoke("c", "fire", "q") != "q:done") return 2;
  if (seen != std::vector<std::string>{"first", "first", "second", "late"}) return 3;
  return 0;
}
