#include "frnl/event_dispatch.hpp"
#include <cassert>
#include <functional>
#include <stdexcept>
#include <string>
static bool rejects(const std::function<void()>& fn) {
  try { fn(); } catch (const std::invalid_argument&) { return true; }
  return false;
}
int main() {
  frnl::EventDispatcher events;
  assert(rejects([&] { events.invoke("missing", "click", "x"); }));
  events.register_component("button", "idle");
  assert(rejects([&] { events.register_component("button", "idle"); }));
  events.define_event("button", "idle", "click", "onClick");
  events.define_event("button", "busy", "finish", "onFinish");
  assert(rejects([&] { events.invoke("button", "click", "x"); }));
  events.register_handler("button", "onClick", [](const std::string& x) { return x + "!"; });
  events.register_handler("button", "onFinish", [](const std::string&) { return "done"; });
  int notifications = 0;
  events.add_listener([&](const std::string& id, const std::string& name,
                          const std::string& payload, const std::string& result) {
    assert(id == "button");
    assert((name == "click" && payload == "hi" && result == "hi!") ||
           (name == "finish" && result == "done"));
    ++notifications;
  });
  assert(events.invoke("button", "click", "hi") == "hi!");
  assert(notifications == 1);
  assert(rejects([&] { events.set_state("button", "unknown"); }));
  events.set_state("button", "busy");
  assert(rejects([&] { events.invoke("button", "click", "hi"); }));
  assert(events.invoke("button", "finish", "") == "done");
  assert(notifications == 2);
  assert(rejects([&] { events.define_event("button", "busy", "finish", "other"); }));
  assert(rejects([&] { events.register_handler("button", "onFinish", [](const std::string&) { return ""; }); }));
  events.register_handler("button", "thrower", [](const std::string&) -> std::string { throw std::runtime_error("failed"); });
  events.define_event("button", "busy", "fail", "thrower");
  try { events.invoke("button", "fail", ""); assert(false); }
  catch (const std::runtime_error&) { assert(notifications == 2); }
}
