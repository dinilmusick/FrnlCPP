#include "frnl/event_dispatch.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <stdexcept>
#include <string>
int main() {
  frnl::EventDispatcher d;
  d.register_component("panel", "idle");
  d.define_event("panel", "idle", "advance", "step");
  d.define_event("panel", "busy", "advance", "step");
  int notifications = 0;
  d.add_listener([&](const std::string&, const std::string&, const std::string&,
                     const std::string&) { ++notifications; });
  d.register_context_handler("panel", "step",
    [&](const std::string& payload, const frnl::EventDispatcher::EventContext& ctx) {
      assert(ctx.component_id == "panel");
      assert(ctx.state == (payload == "first" ? "idle" : "busy"));
      if (payload == "first") {
        ctx.set_state("busy");
        assert(ctx.state == "idle"); // Captured context remains immutable.
      } else {
        bool rejected = false;
        try { ctx.set_state("missing"); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
      }
      return payload + "!";
    });
  assert(d.invoke("panel", "advance", "first") == "first!");
  assert(d.invoke("panel", "advance", "second") == "second!");
  assert(notifications == 2);
  bool duplicate = false;
  try { d.register_handler("panel", "step", [](const std::string&) { return "wrong"; }); }
  catch (const std::invalid_argument&) { duplicate = true; }
  assert(duplicate);
  return 0;
}
