#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// A nested event can fail in a listener without poisoning subsequent dispatch.
// New subscriptions are visible to later events, not the failing snapshot.
int main() {
  frnl::EventDispatcher d;
  d.register_component("c", "ready");
  d.define_event("c", "ready", "outer", "outer_handler");
  d.define_event("c", "ready", "inner", "inner_handler");
  d.register_handler("c", "inner_handler", [](const std::string& p) { return p + ":inner"; });
  d.register_handler("c", "outer_handler", [&](const std::string& p) {
    return d.invoke("c", "inner", p) + ":outer";
  });
  std::vector<std::string> seen;
  bool fail = true;
  d.add_listener([&](const std::string&, const std::string& event,
                     const std::string&, const std::string&) {
    seen.push_back(event);
    if (event == "inner" && fail) {
      fail = false;
      d.add_listener([&](const std::string&, const std::string& event,
                         const std::string&, const std::string&) {
        seen.push_back("late:" + event);
      });
      throw std::runtime_error("inner listener failed");
    }
  });
  bool caught = false;
  try { (void)d.invoke("c", "outer", "first"); }
  catch (const std::runtime_error&) { caught = true; }
  if (!caught || seen != std::vector<std::string>{"inner"}) return 1;
  if (d.invoke("c", "outer", "second") != "second:inner:outer") return 2;
  if (seen != std::vector<std::string>{"inner", "inner", "late:inner", "outer", "late:outer"}) return 3;
  return 0;
}
