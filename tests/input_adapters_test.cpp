#include "frnl/input_adapters.hpp"
#include <cassert>
#include <stdexcept>
#include <string>
using namespace frnl;
template<class Fn> void rejects(Fn fn) { bool threw=false;
  try { fn(); } catch (const std::exception&) { threw=true; } assert(threw); }
int main() {
  SemanticRuntime runtime;
  ComponentDefinition root;
  root.name="Root";
  root.states["active"].events["submit"]="submit";
  runtime.register_component("root", root, "active");
  runtime.register_handler("submit", [](SemanticRuntime&, const std::string&, const std::any& payload) {
    return std::any(std::any_cast<int>(payload) * 2);
  });
  InputAdapter input(runtime);
  input.bind("keyboard.enter", {"root"}, "submit",
    [](const std::any& p) { return p.type() == typeid(int) && std::any_cast<int>(p) > 0; },
    [](const std::any& raw) { return std::any(std::stoi(std::any_cast<std::string>(raw))); });
  assert(std::any_cast<int>(input.dispatch("keyboard.enter", std::string("4"))) == 8);
  rejects([&]{ input.dispatch("keyboard.enter", std::string("0")); });
  rejects([&]{ input.dispatch("keyboard.enter", std::string("bad")); });
  rejects([&]{ input.dispatch("missing", 3); });
  rejects([&]{ input.bind("forged", {"not-root"}, "submit"); });
  rejects([&]{ input.bind("keyboard.enter", {"root"}, "submit"); });
  assert(input.unbind("keyboard.enter"));
  assert(!input.unbind("keyboard.enter"));
  rejects([&]{ input.dispatch("keyboard.enter", std::string("4")); });

  bool invoked=false;
  runtime.register_handler("outer", [](SemanticRuntime&, const std::string&, const std::any&) { return std::any(); });
  input.bind("app.input", {"root"}, "submit", {},
    [&](const std::any& raw) { invoked=true; input.unbind("app.input"); return raw; });
  assert(std::any_cast<int>(input.dispatch("app.input", 5)) == 10);
  assert(invoked);
}
