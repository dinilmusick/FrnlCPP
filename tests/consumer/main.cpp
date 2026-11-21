#include <frnl/semantic_runtime.hpp>
#include <frnl/lifecycle.hpp>
#include <frnl/event_dispatch.hpp>
#include <cassert>
int main() {
  frnl::SemanticRuntime runtime;
  frnl::ComponentDefinition panel;
  panel.name = "ExternalPanel";
  panel.states["idle"] = {};
  runtime.register_component("main", panel, "idle");
  assert(runtime.component_path("main").size() == 1);
  frnl::Lifecycle lifecycle;
  lifecycle.transition(frnl::LifecyclePhase::initializing);
  assert(lifecycle.phase() == frnl::LifecyclePhase::initializing);
  frnl::EventDispatcher legacy;
  legacy.register_component("old", "idle");
}
