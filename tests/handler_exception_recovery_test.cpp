#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// Handler failure must not notify listeners, and subscriptions made by the
// failing handler must only become visible on a subsequent successful dispatch.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("c", "ready");
  dispatcher.define_event("c", "ready", "fire", "handler");
  bool fail_once = true;
  std::vector<std::string> seen;
  dispatcher.register_handler("c", "handler", [&](const std::string& payload) {
    if (fail_once) {
      fail_once = false;
      dispatcher.add_listener([&](const std::string&, const std::string&,
                                  const std::string&, const std::string&) {
        seen.push_back("late");
      });
      throw std::runtime_error("handler failure");
    }
    return payload + ":ok";
  });
  dispatcher.add_listener([&](const std::string&, const std::string&,
                              const std::string&, const std::string&) {
    seen.push_back("first");
  });
  bool caught = false;
  try { (void)dispatcher.invoke("c", "fire", "a"); }
  catch (const std::runtime_error&) { caught = true; }
  if (!caught || !seen.empty()) return 1;
  if (dispatcher.invoke("c", "fire", "b") != "b:ok") return 2;
  if (seen != std::vector<std::string>{"first", "late"}) return 3;
  return 0;
}
