#include "frnl/event_dispatch.hpp"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

// A failed handler must not notify listeners; subscriptions made before
// failure remain registered for subsequent successful invocations.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("node", "ready");
  dispatcher.define_event("node", "ready", "run", "handler");
  std::vector<std::string> observed;
  dispatcher.add_listener([&](const std::string&, const std::string&,
                              const std::string&, const std::string& result) {
    observed.push_back("original:" + result);
  });
  bool first = true;
  dispatcher.register_handler("node", "handler", [&](const std::string& value) {
    if (first) {
      first = false;
      dispatcher.add_listener([&](const std::string&, const std::string&,
                                  const std::string&, const std::string& result) {
        observed.push_back("late:" + result);
      });
      throw std::runtime_error("handler failure");
    }
    return value + "-ok";
  });
  bool threw = false;
  try { (void)dispatcher.invoke("node", "run", "first"); }
  catch (const std::runtime_error&) { threw = true; }
  assert(threw);
  assert(observed.empty());
  assert(dispatcher.invoke("node", "run", "second") == "second-ok");
  assert((observed == std::vector<std::string>{"original:second-ok", "late:second-ok"}));
}
