#include "frnl/lifecycle.hpp"
#include <cassert>
#include <stdexcept>
#include <vector>
using namespace frnl;
int main() {
  using P = LifecyclePhase;
  assert(can_transition(P::created, P::initializing));
  assert(!can_transition(P::created, P::active));
  for (const auto from : {P::created, P::initializing, P::mounted,
                          P::active, P::suspended, P::resuming,
                          P::disposing, P::disposed, P::failed}) {
    for (const auto to : {P::created, P::initializing, P::mounted,
                          P::active, P::suspended, P::resuming,
                          P::disposing, P::disposed, P::failed}) {
      Lifecycle lifecycle(from);
      if (from == to || can_transition(from, to)) {
        lifecycle.transition(to);
        assert(lifecycle.phase() == to);
      } else {
        bool rejected = false;
        try { lifecycle.transition(to); } catch (const std::logic_error&) { rejected = true; }
        assert(rejected && lifecycle.phase() == from);
      }
    }
  }
  Lifecycle lifecycle;
  int first = 0, second = 0;
  LifecycleSubscription second_token;
  auto first_token = lifecycle.subscribe([&](const LifecycleTransition& transition) {
    ++first;
    assert(transition.from == P::created && transition.to == P::initializing);
    second_token = lifecycle.subscribe([&](const LifecycleTransition&) { ++second; });
  });
  lifecycle.transition(P::initializing);
  assert(first == 1 && second == 0); // late subscription excluded from snapshot
  first_token.reset();
  lifecycle.transition(P::mounted);
  assert(first == 1 && second == 1);
  second_token.reset();
  lifecycle.transition(P::active);
  assert(second == 1);
  LifecycleSubscription detached;
  {
    Lifecycle temporary;
    detached = temporary.subscribe([](const LifecycleTransition&){});
  }
  detached.reset();
  int recovery = 0;
  Lifecycle failing;
  auto throws = failing.subscribe([](const LifecycleTransition&) {
    throw std::runtime_error("observer");
  });
  bool caught = false;
  try { failing.transition(P::initializing); }
  catch (const std::runtime_error&) { caught = true; }
  assert(caught && failing.phase() == P::initializing);
  throws.reset();
  auto succeeding = failing.subscribe([&](const LifecycleTransition&) { ++recovery; });
  failing.transition(P::mounted);
  assert(recovery == 1);
  succeeding.reset();
}
