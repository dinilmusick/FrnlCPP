#include "frnl/event_dispatch.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// A failing nested handler must not notify listeners or poison later dispatch.
int main() {
  frnl::EventDispatcher d;
  d.register_component("component", "ready");
  d.define_event("component", "ready", "fail", "fail_handler");
  d.define_event("component", "ready", "recover", "recover_handler");
  d.register_handler("component", "fail_handler", [](const std::string&) -> std::string {
    throw std::runtime_error("expected failure");
  });
  d.register_handler("component", "recover_handler", [](const std::string& x) {
    return x + ":ok";
  });
  std::vector<std::string> seen;
  d.add_listener([&](const std::string&, const std::string& event,
                     const std::string&, const std::string&) { seen.push_back(event); });
  bool failed = false;
  try { d.invoke("component", "fail", "x"); }
  catch (const std::runtime_error&) { failed = true; }
  if (!failed || !seen.empty()) return 1;
  if (d.invoke("component", "recover", "x") != "x:ok") return 2;
  if (seen != std::vector<std::string>{"recover"}) return 3;
  return 0;
}
