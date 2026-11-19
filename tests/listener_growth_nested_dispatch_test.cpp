#include "frnl/event_dispatch.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>

int main() {
  frnl::EventDispatcher dispatcher;
  dispatcher.register_component("control", "ready");
  dispatcher.define_event("control", "ready", "outer", "outer_handler");
  dispatcher.define_event("control", "ready", "inner", "inner_handler");
  dispatcher.register_handler("control", "outer_handler", [](const std::string&) { return "outer-ok"; });
  dispatcher.register_handler("control", "inner_handler", [](const std::string&) { return "inner-ok"; });

  std::vector<std::string> seen;
  bool nested_once = false;
  dispatcher.add_listener([&](const std::string&, const std::string& event,
                              const std::string&, const std::string&) {
    seen.push_back("original:" + event);
    if (event == "outer" && !nested_once) {
      nested_once = true;
      for (int i = 0; i < 64; ++i) {
        dispatcher.add_listener([&, i](const std::string&, const std::string& name,
                                       const std::string&, const std::string&) {
          seen.push_back(std::to_string(i) + ":" + name);
        });
      }
      assert(dispatcher.invoke("control", "inner", "") == "inner-ok");
    }
  });
  assert(dispatcher.invoke("control", "outer", "") == "outer-ok");
  assert(seen.size() == 66);
  assert(seen.front() == "original:outer");
  assert(seen[1] == "original:inner");
  for (int i = 0; i < 64; ++i) {
    assert(seen[static_cast<std::size_t>(i + 2)] == std::to_string(i) + ":inner");
  }
  seen.clear();
  assert(dispatcher.invoke("control", "outer", "") == "outer-ok");
  assert(seen.size() == 65);
  assert(seen.front() == "original:outer");
  for (int i = 0; i < 64; ++i) {
    assert(seen[static_cast<std::size_t>(i + 1)] == std::to_string(i) + ":outer");
  }
}
