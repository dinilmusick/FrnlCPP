#include "frnl/semantic_runtime.hpp"
#include <cassert>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>
using namespace frnl;
namespace {
template <typename Fn> void rejects(Fn fn) {
  bool thrown = false;
  try { fn(); } catch (const std::exception&) { thrown = true; }
  assert(thrown);
}
ComponentDefinition button_definition() {
  ComponentDefinition result;
  result.name = "Button";
  result.presentation_role = "button";
  result.states["idle"].events["click"] = "idle.click";
  result.states["idle"].visual_parameters["opacity"] = 0.25;
  result.states["idle"].phases.release_seconds = 0.25;
  result.states["active"].events["click"] = "active.click";
  result.states["active"].visual_parameters["opacity"] = 0.9;
  result.states["active"].phases.attack_seconds = 0.5;
  return result;
}
}
int main() {
  SemanticRuntime runtime;
  ComponentDefinition root;
  root.name = "Panel";
  root.states["idle"] = {};
  runtime.register_component("root", root, "idle");
  runtime.register_component("button", button_definition(), "idle", "root");
  auto incoming = button_definition();
  incoming.states["active"].phases.attack_seconds = 1.0;
  runtime.register_component("entering", incoming, "active", "root");
  assert(runtime.current_phase("entering") == Phase::attack);
  runtime.advance("entering", 1.0);
  assert(runtime.current_phase("entering") == Phase::sustain);
  runtime.remove_component("entering");
  assert((runtime.component_path("button") == std::vector<std::string>{"root", "button"}));
  assert(runtime.component_at_path({"root", "button"}) == "button");
  rejects([&]{ runtime.component_at_path({"button"}); });
  rejects([&]{ runtime.component_at_path({}); });
  rejects([&]{ runtime.register_component("orphan", root, "idle", "missing"); });
  rejects([&]{ runtime.register_component("root", root, "idle"); });
  rejects([&]{ runtime.register_component("bad", root, "missing"); });
  rejects([&]{ runtime.remove_component("root"); });

  auto theme = Theme{};
  theme["Button"]["idle"]["opacity"] = 0.7;
  theme["Button"]["active"]["opacity"] = 0.8;
  runtime.register_theme("dark", theme);
  runtime.apply_theme("root", "dark"); // inherited by descendant
  assert(std::get<double>(runtime.visual_parameters("button").at("opacity")) == 0.7);
  runtime.set_visual_override("button", "opacity", 1.0);
  assert(std::get<double>(runtime.visual_parameters("button").at("opacity")) == 1.0);
  runtime.set_property("button", "label", std::string("Go"));
  assert(std::get<std::string>(runtime.properties("button").at("label")) == "Go");
  runtime.set_component_data("button", "counter", 7);
  runtime.set_page_data("session", std::string("one"));
  assert(std::any_cast<int>(runtime.component_data("button", "counter")) == 7);
  assert(std::any_cast<std::string>(runtime.page_data("session")) == "one");

  int update_count = 0;
  int late_update_count = 0;
  std::vector<Subscription> dynamic_updates;
  auto updates = runtime.on_update([&](const Update& notice) {
    ++update_count;
    if (notice.kind == "state")
      dynamic_updates.emplace_back(runtime.on_update([&](const Update&) { ++late_update_count; }));
  });
  runtime.set_state("button", "active");
  assert(runtime.current_phase("button") == Phase::release);
  assert(runtime.current_state("button") == "idle");
  runtime.advance("button", 0.25);
  assert(runtime.current_state("button") == "active");
  assert(runtime.current_phase("button") == Phase::attack);
  runtime.advance("button", 0.5);
  assert(runtime.current_phase("button") == Phase::sustain);
  runtime.advance("button", 1.5);
  assert(runtime.phase_elapsed("button") == 0.5);
  assert(update_count > 1 && late_update_count > 0);
  rejects([&]{ runtime.advance("button", -0.1); });
  rejects([&]{ runtime.set_state("button", "unknown"); });
  assert(std::get<double>(runtime.visual_parameters("button").at("opacity")) == 1.0);

  // Callbacks and listener sets are copied before running arbitrary user code.
  int outer_calls = 0, regular_calls = 0, new_calls = 0;
  bool nested_once = false;
  std::vector<Subscription> dynamic_events;
  runtime.register_handler("idle.click", [](SemanticRuntime&, const std::string&, const std::any&) {
    return std::any(std::string("idle"));
  });
  runtime.register_handler("active.click", [&](SemanticRuntime& r, const std::string& id,
                                                const std::any& payload) {
    const int value = std::any_cast<int>(payload);
    if (value == 1 && !nested_once) {
      nested_once = true;
      dynamic_events.emplace_back(r.on_event([&](const EventNotice&) { ++new_calls; }));
      auto nested = r.invoke(id, "click", 2);
      assert(std::any_cast<int>(nested) == 20);
    }
    return std::any(value * 10);
  });
  auto first = runtime.on_event([&](const EventNotice& notice) {
    ++outer_calls;
    assert(notice.state_at_entry == "active");
  });
  auto second = runtime.on_event([&](const EventNotice&) { ++regular_calls; });
  assert(runtime.diagnostics().empty());
  assert(std::any_cast<int>(runtime.semantic_input({"root", "button"}, "click", 1)) == 10);
  assert(outer_calls == 2 && regular_calls == 2 && new_calls == 1);
  first.reset();
  assert(std::any_cast<int>(runtime.invoke("button", "click", 3)) == 30);
  assert(outer_calls == 2 && regular_calls == 3 && new_calls == 2);
  rejects([&]{ runtime.invoke("button", "missing", 1); });
  rejects([&]{ runtime.semantic_input({"button"}, "click", 1); });
  auto failed = button_definition();
  failed.states["idle"].events["broken"] = "unregistered";
  runtime.register_component("diagnostic", failed, "idle");
  assert(runtime.diagnostics().size() == 1);

  int step_sequence = 0;
  runtime.register_library("sum", [](const std::vector<std::any>& values) {
    return std::any(std::any_cast<int>(values.at(0)) + std::any_cast<int>(values.at(1)));
  });
  assert(std::any_cast<int>(runtime.call_library("sum", {3, 4})) == 7);
  runtime.register_flow("submit", {
    {"last", {"middle"}, [&](SemanticRuntime&) { assert(step_sequence == 2); ++step_sequence; }},
    {"middle", {"first"}, [&](SemanticRuntime& r) {
      assert(step_sequence == 1); ++step_sequence; r.set_page_data("completed", true);
    }},
    {"first", {}, [&](SemanticRuntime&) { assert(step_sequence == 0); ++step_sequence; }}
  });
  runtime.execute_flow("submit");
  assert(step_sequence == 3 && std::any_cast<bool>(runtime.page_data("completed")));
  rejects([&]{ runtime.register_flow("cycle", {
    {"a", {"b"}, [](SemanticRuntime&){}}, {"b", {"a"}, [](SemanticRuntime&){}}
  }); });
  rejects([&]{ runtime.register_flow("unknown", {
    {"a", {"missing"}, [](SemanticRuntime&){}}
  }); });
  runtime.register_flow("throw", {{"failing", {}, [](SemanticRuntime&) {
    throw std::runtime_error("host failure");
  }}});
  rejects([&]{ runtime.execute_flow("throw"); });

  int scheduled = 0;
  std::map<std::string, std::any> disk;
  runtime.set_host_ports({
    [&](std::function<void()> callback) { callback(); ++scheduled; },
    [] { return 12.5; },
    [&](const std::string& key, const std::any& val) { disk[key] = val; },
    [&](const std::string& key) { return disk.at(key); }
  });
  runtime.schedule_on_ui([&]{ runtime.set_component_data("button", "scheduled", true); });
  assert(scheduled == 1 && runtime.monotonic_time() == 12.5);
  runtime.persist("key", 42);
  assert(std::any_cast<int>(runtime.restore("key")) == 42);
  runtime.remove_component("diagnostic");
  runtime.remove_component("button");
  runtime.remove_component("root");
  assert(!runtime.has_component("root"));
  updates.reset();
  second.reset();
  dynamic_updates.clear();
  dynamic_events.clear();

  Subscription lifetime;
  {
    SemanticRuntime ephemeral;
    lifetime = ephemeral.on_update([](const Update&){});
  }
  lifetime.reset(); // no use after free
}
