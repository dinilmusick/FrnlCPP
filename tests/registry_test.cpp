#include "frnl/registry.hpp"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>
using namespace frnl;
namespace { template<typename T> void rejects(T f) { bool rejected=false;
  try { f(); } catch(const std::exception&) { rejected=true; } assert(rejected); } }
int main() {
  DependencyGraph graph;
  graph.register_node({"ui", 1, {"render"}, {"draw"}});
  graph.register_node({"backend", 1, {}, {"render"}});
  assert(graph.resolve_dependency("render") == "backend");
  assert((graph.topological_order() == std::vector<std::string>{"backend", "ui"}));
  assert(graph.validate().empty());
  rejects([&]{ graph.register_node({"ui", 1, {}, {}}); });
  graph.register_node({"broken", 1, {"missing"}, {}});
  assert(graph.validate().size() == 1);
  graph.register_node({"cycleA", 1, {"cycleB"}, {}});
  graph.register_node({"cycleB", 1, {"cycleA"}, {}});
  rejects([&]{ graph.topological_order(); });
  graph.clear();
  assert(graph.validate().empty());

  Registry registry;
  Registration renderer;
  renderer.id="renderer"; renderer.version=1;
  renderer.capabilities.push_back({"renderer-impl", 1, {"render"}, {}});
  renderer.value=std::string("native");
  registry.register_entry(renderer);
  Registration widget;
  widget.id="widget"; widget.version=2;
  widget.contract=Contract{"widget", 2, {}, {}, {"render"}};
  widget.value=22;
  registry.register_entry(widget);
  assert(registry.resolve<std::string>("renderer") == "native");
  assert(registry.resolve<int>("widget") == 22);
  assert((registry.ids() == std::vector<std::string>{"renderer", "widget"}));
  registry.require_capabilities("widget", {"render"});
  rejects([&]{ registry.require_capabilities("widget", {}); });
  rejects([&]{ registry.resolve<int>("missing"); });
  rejects([&]{ registry.register_entry({"widget", 1, {}, {}, 1}); });
  rejects([&]{ registry.register_entry({"invalid", 0, {}, {}, 1}); });
  widget.version=3; registry.register_entry(widget);
  assert(registry.at("widget").version == 3);
  auto dep=registry.dependency_graph();
  assert(dep.validate().empty());
  assert((dep.topological_order() == std::vector<std::string>{"renderer", "widget"}));
  registry.clear();
  assert(registry.ids().empty());
}
