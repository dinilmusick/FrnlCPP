#include "frnl/slots.hpp"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>
using namespace frnl;
namespace {template <typename Fn> void rejects(Fn fn) {bool threw=false;
  try {fn();} catch(const std::exception&){threw=true;} assert(threw);}}
int main() {
  SemanticRuntime runtime;
  ComponentDefinition parent; parent.name="Panel"; parent.states["idle"]={};
  ComponentDefinition child; child.name="Button"; child.states["idle"]={};
  child.states["active"]={};
  runtime.register_component("parent",parent,"idle");
  runtime.register_component("item",child,"idle","parent");
  SlotModel slots(runtime);
  slots.define_slot("parent","header");
  slots.define_slot("parent","rows",true);
  rejects([&]{slots.define_slot("missing","bad");});
  rejects([&]{slots.define_slot("parent","header");});
  rejects([&]{slots.set_occupant("parent","header","missing");});
  rejects([&]{slots.append("parent","header","item");});
  std::vector<SlotEventType> events;
  SlotSubscription transient;
  auto listener = slots.subscribe([&](const SlotEvent& event) {
    events.push_back(event.type);
    if (event.type == SlotEventType::occupant_changed) {
      transient = slots.subscribe([&](const SlotEvent&) { events.push_back(SlotEventType::property_changed); });
    }
  });
  slots.set_occupant("parent","header","item");
  assert(events.size()==1 && events[0]==SlotEventType::occupant_changed);
  slots.set_property("parent","header","visible",true);
  assert(std::get<bool>(slots.at("parent","header").properties.at("visible")));
  assert(events.size()==3);
  slots.append("parent","rows","item");
  assert(slots.at("parent","rows").collection.size()==1);
  rejects([&]{slots.append("parent","rows","item");});
  runtime.set_state("item","active");
  // Header occupant and repeatable collection both observe occupant state, without taking ownership.
  assert(events.size()==9);
  assert(events[7]==SlotEventType::state_changed);
  assert(events[8]==SlotEventType::property_changed);
  assert(slots.remove("parent","rows","item"));
  assert(!slots.remove("parent","rows","item"));
  transient.reset();
  listener.reset();
  SlotSubscription late;
  {
    SlotModel temporary(runtime);
    late=temporary.subscribe([](const SlotEvent&){});
  }
  late.reset();
}
