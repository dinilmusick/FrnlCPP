#include "frnl/event_dispatch.hpp"
#include <cassert>
#include <stdexcept>
#include <string>
int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("panel", "ready");
  dispatcher.define_event("panel", "ready", "click", "handle");
  dispatcher.register_handler("panel", "handle", [](const std::string& x) { return x; });
  int first = 0, second = 0, late = 0;
  frnl::EventDispatcher::SubscriptionId first_id = 0;
  first_id = dispatcher.subscribe([&](const std::string&, const std::string&,
                                      const std::string&, const std::string&) {
    ++first;
    assert(dispatcher.unsubscribe(first_id));
    assert(!dispatcher.unsubscribe(first_id));
    dispatcher.subscribe([&](const std::string&, const std::string&,
                             const std::string&, const std::string&) { ++late; });
  });
  dispatcher.subscribe([&](const std::string&, const std::string&,
                           const std::string&, const std::string&) { ++second; });
  assert(dispatcher.invoke("panel", "click", "a") == "a");
  assert(first == 1 && second == 1 && late == 0);
  assert(dispatcher.invoke("panel", "click", "b") == "b");
  assert(first == 1 && second == 2 && late == 1);
  assert(!dispatcher.unsubscribe(0));
  bool rejected = false;
  try { dispatcher.subscribe({}); } catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected);
}