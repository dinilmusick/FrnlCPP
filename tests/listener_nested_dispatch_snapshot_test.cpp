#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// A listener may synchronously invoke another component, then register a new
// listener. The nested dispatch uses its own snapshot; the outer dispatch
// continues with the original snapshot and retains notification order.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("outer", "ready");
  dispatcher.register_component("inner", "ready");
  dispatcher.define_event("outer", "ready", "fire", "run");
  dispatcher.define_event("inner", "ready", "fire", "run");
  dispatcher.register_handler("outer", "run", [](const std::string& x) { return x + ":outer"; });
  dispatcher.register_handler("inner", "run", [](const std::string& x) { return x + ":inner"; });
  std::vector<std::string> seen;
  bool nested = false;
  dispatcher.add_listener([&](const std::string& id, const std::string&,
                              const std::string&, const std::string&) {
    seen.push_back("first:" + id);
    if (id == "outer" && !nested) {
      nested = true;
      if (dispatcher.invoke("inner", "fire", "x") != "x:inner")
        throw std::runtime_error("nested return mismatch");
      dispatcher.add_listener([&](const std::string& next_id, const std::string&,
                                  const std::string&, const std::string&) {
        seen.push_back("late:" + next_id);
      });
    }
  });
  dispatcher.add_listener([&](const std::string& id, const std::string&,
                              const std::string&, const std::string&) {
    seen.push_back("second:" + id);
  });
  if (dispatcher.invoke("outer", "fire", "x") != "x:outer") return 1;
  if (seen != std::vector<std::string>{"first:outer", "first:inner",
                                        "second:inner", "second:outer"}) return 2;
  if (dispatcher.invoke("inner", "fire", "y") != "y:inner") return 3;
  if (seen != std::vector<std::string>{"first:outer", "first:inner",
                                        "second:inner", "second:outer",
                                        "first:inner", "second:inner", "late:inner"}) return 4;
  return 0;
}
