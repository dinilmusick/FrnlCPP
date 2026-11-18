#include "frnlcpp/component_scope.hpp"
#include <cassert>
#include <stdexcept>
#include <vector>
namespace {
template <typename F> void rejects(F f) {
  bool threw = false;
  try { f(); } catch (const std::exception&) { threw = true; }
  assert(threw);
}
}
int main() {
  frnlcpp::ComponentScope scope;
  scope.register_component("root");
  scope.register_component("child", "root");
  scope.register_component("leaf", "child");
  assert((scope.resolve_path("leaf") == std::vector<std::string>{"root", "child", "leaf"}));
  assert(scope.component_at_path({"root", "child", "leaf"}) == "leaf");
  rejects([&] { scope.component_at_path({"root", "leaf"}); });
  rejects([&] { scope.component_at_path({}); });
  rejects([&] { scope.resolve_path("missing"); });
  rejects([&] { scope.register_component("self", "self"); });
  scope.register_component("a", "b");
  scope.register_component("b", "a");
  rejects([&] { scope.resolve_path("a"); });
  scope.register_component("orphan", "not-registered");
  rejects([&] { scope.resolve_path("orphan"); });
  scope.register_component("leaf", "root");
  rejects([&] { scope.component_at_path({"root", "child", "leaf"}); });
}
