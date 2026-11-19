#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// A listener registered during notification must participate in a nested
// dispatch started afterwards, but never in the already-running outer snapshot.
// This also catches invalidated iterators when the listener vector reallocates.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("component", "ready");
  dispatcher.define_event("component", "ready", "outer", "run");
  dispatcher.define_event("component", "ready", "inner", "run");
  dispatcher.register_handler("component", "run",
    [](const std::string& value) { return value + ":ok"; });

  std::vector<std::string> seen;
  bool entered = false;
  dispatcher.add_listener([&](const std::string&, const std::string& event,
                              const std::string&, const std::string&) {
    seen.push_back("first:" + event);
    if (event == "outer" && !entered) {
      entered = true;
      // Force reallocations while the outer listener is still executing.
      for (int i = 0; i < 32; ++i) {
        dispatcher.add_listener([&, i](const std::string&, const std::string& nested,
                                        const std::string&, const std::string&) {
          if (i == 31) seen.push_back("late:" + nested);
        });
      }
      if (dispatcher.invoke("component", "inner", "nested") != "nested:ok")
        throw std::runtime_error("nested return mismatch");
    }
  });
  dispatcher.add_listener([&](const std::string&, const std::string& event,
                              const std::string&, const std::string&) {
    seen.push_back("second:" + event);
  });

  if (dispatcher.invoke("component", "outer", "input") != "input:ok") return 1;
  const std::vector<std::string> expected{
    "first:outer", "first:inner", "second:inner", "late:inner", "second:outer"};
  if (seen != expected) return 2;
  // Late subscribers persist for later top-level dispatches. They must not
  // be duplicated by replaying the original callback, and the nested event
  // must not be triggered again after the one-time registration.
  if (dispatcher.invoke("component", "outer", "again") != "again:ok") return 3;
  const std::vector<std::string> expected_after_repeat{
    "first:outer", "first:inner", "second:inner", "late:inner", "second:outer",
    "first:outer", "second:outer", "late:outer"};
  if (seen != expected_after_repeat) return 4;
  return 0;
}
