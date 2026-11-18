#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// Nested invocation must not corrupt the outer listener snapshot.
int main() {
  frnl::EventDispatcher d;
  d.register_component("c", "ready");
  d.define_event("c", "ready", "outer", "o");
  d.define_event("c", "ready", "inner", "i");
  d.register_handler("c", "i", [](const std::string& x) { return x + ":inner"; });
  d.register_handler("c", "o", [&](const std::string& x) {
    d.add_listener([](const std::string&, const std::string&, const std::string&, const std::string&) {});
    if (d.invoke("c", "inner", x) != x + ":inner") throw std::runtime_error("nested result");
    return x + ":outer";
  });
  std::vector<std::string> seen;
  d.add_listener([&](const std::string&, const std::string& event, const std::string&, const std::string& result) {
    seen.push_back(event + ":" + result);
  });
  if (d.invoke("c", "outer", "p") != "p:outer") return 1;
  if (seen != std::vector<std::string>{"inner:p:inner", "outer:p:outer"}) return 2;
  return 0;
}
