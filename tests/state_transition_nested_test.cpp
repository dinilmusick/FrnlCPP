#include "frnl/event_dispatch.hpp"
#include <string>
#include <vector>

// State changes and nested dispatch must not change the outer event's
// resolved handler or its listener snapshot.
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("component", "idle");
  dispatcher.define_event("component", "idle", "start", "on_start");
  dispatcher.define_event("component", "active", "finish", "on_finish");
  dispatcher.register_handler("component", "on_finish", [](const std::string& p) {
    return "finished:" + p;
  });
  std::vector<std::string> calls;
  dispatcher.register_handler("component", "on_start", [&](const std::string& p) {
    dispatcher.set_state("component", "active");
    if (dispatcher.invoke("component", "finish", p) != "finished:" + p)
      throw std::runtime_error("nested dispatch result mismatch");
    return "started:" + p;
  });
  dispatcher.add_listener([&](const std::string&, const std::string& event,
                              const std::string&, const std::string&) {
    calls.push_back("first:" + event);
    if (event == "finish")
      dispatcher.add_listener([&](const std::string&, const std::string& nested,
                                  const std::string&, const std::string&) {
        calls.push_back("late:" + nested);
      });
  });
  if (dispatcher.invoke("component", "start", "x") != "started:x") return 1;
  if (calls != std::vector<std::string>{"first:finish", "first:start"}) return 2;
  if (dispatcher.invoke("component", "finish", "y") != "finished:y") return 3;
  if (calls != std::vector<std::string>{"first:finish", "first:start",
                                        "first:finish", "late:finish"}) return 4;
  return 0;
}
