#include <frnl/semantic_runtime.hpp>
#include <frnl/lifecycle.hpp>
#include <frnl/event_dispatch.hpp>
#include <frnl/ui_session.hpp>
#include <frnl/routing.hpp>
#include <frnl/resources.hpp>
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
  auto scope = frnl::Scope::create("external");
  auto lease = scope->add<int>({"native", [](const frnl::ResourceContext&) { return 10; }, {}});
  assert(lease->load() == 10);
  scope->dispose();
  assert(lease->status() == frnl::ResourceStatus::released);
  frnl::FrontendApi api(runtime, "consumer");
  assert(api.inspect().component_count == 1);
  frnl::Router router;
  router.add_route({"page", "/page/:name", {}});
  assert(router.match("/page/home") && router.match("/page/home")->params.at("name") == "home");
  frnl::EventDispatcher legacy;
  legacy.register_component("old", "idle");
}
