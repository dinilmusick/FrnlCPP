#include "frnl/resources.hpp"
#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace frnl;
template <class Fn> void fails(Fn fn, const std::string& code) {
  bool threw = false;
  try { fn(); } catch (const Fault& e) { threw = true; assert(e.code() == code); }
  assert(threw);
}
int main() {
  auto effect = define_effect<int>("load-profile", [](const CancellationToken& signal) {
    if (signal.aborted()) throw std::logic_error("abort");
    return 17;
  });
  CancellationToken token;
  assert(effect.run(token) == 17);
  token.cancel();
  assert(token.aborted());
  bool invalid = false;
  try { (void)define_effect<int>("", [](const CancellationToken&) { return 0; }); }
  catch (const std::invalid_argument&) { invalid = true; }
  assert(invalid);
  auto command = define_command<int>("save", 7);
  assert(command.id == "save" && *command.payload == 7);
  try { throw Fault("INVARIANT", "error", true, false, std::make_exception_ptr(std::logic_error("cause"))); }
  catch (const Fault& e) {
    assert(e.code() == "INVARIANT" && e.recoverable() && !e.retryable());
    assert(static_cast<bool>(e.cause()));
  }

  int acquired = 0, released = 0;
  auto root = Scope::create("page");
  auto child = root->create_child("component");
  auto resource = child->add<int>(Resource<int>{
    "connection",
    [&](const ResourceContext& ctx) {
      assert(ctx.scope_id == "component" && !ctx.signal.aborted());
      return ++acquired * 3;
    },
    [&](int& value, const ResourceContext& ctx) {
      assert(value == 3 && ctx.signal.aborted());
      ++released;
    }});
  assert(resource->status() == ResourceStatus::idle);
  assert(resource->load() == 3);
  assert(resource->load() == 3 && acquired == 1);
  assert(resource->status() == ResourceStatus::ready);
  root->dispose();
  assert(root->disposed() && child->disposed() && child->signal().aborted());
  assert(resource->status() == ResourceStatus::released && released == 1);
  root->dispose();
  resource->release();
  assert(released == 1);
  fails([&] { (void)resource->load(); }, "RESOURCE_RELEASED");
  fails([&] { root->create_child("no"); }, "SCOPE_DISPOSED");
  fails([&] { (void)root->add<int>({"no", [](const ResourceContext&) { return 1; }, {}}); },
        "SCOPE_DISPOSED");

  int attempts = 0, retry_cleanup = 0;
  auto retry = Scope::create("retry");
  auto flaky = retry->add<int>({"db", [&](const ResourceContext&) {
    if (++attempts == 1) throw std::runtime_error("temporary failure");
    return 9;
  }, [&](int& v, const ResourceContext&) { assert(v == 9); ++retry_cleanup; }});
  fails([&] { (void)flaky->load(); }, "RESOURCE_ACQUIRE_FAILED");
  assert(flaky->status() == ResourceStatus::error && flaky->error()->retryable());
  assert(static_cast<bool>(flaky->error()->cause()));
  assert(flaky->load() == 9 && attempts == 2);
  retry->dispose();
  assert(retry_cleanup == 1 && flaky->status() == ResourceStatus::released);

  int after_abort_cleanup = 0;
  auto abort_scope = Scope::create("abort");
  auto during = abort_scope->add<int>({
    "abort-during-acquire",
    [&](const ResourceContext&) { abort_scope->dispose(); return 22; },
    [&](int& v, const ResourceContext& c) { assert(v == 22 && c.signal.aborted()); ++after_abort_cleanup; }});
  fails([&] { (void)during->load(); }, "RESOURCE_ABORTED");
  assert(after_abort_cleanup == 1 && during->status() == ResourceStatus::released);

  int done = 0;
  auto failure_scope = Scope::create("errors");
  auto a = failure_scope->add<int>({"broken-release", [](const ResourceContext&) { return 1; },
    [](int&, const ResourceContext&) { throw std::runtime_error("release failure"); }});
  auto b = failure_scope->add<int>({"later-release", [](const ResourceContext&) { return 2; },
    [&](int&, const ResourceContext&) { ++done; }});
  (void)a->load(); (void)b->load();
  fails([&] { failure_scope->dispose(); }, "RESOURCE_RELEASE_FAILED");
  assert(a->status() == ResourceStatus::released && a->error()->code() == "RESOURCE_RELEASE_FAILED");
  assert(b->status() == ResourceStatus::released && done == 1);
  failure_scope->dispose();

  int abandoned_release = 0;
  std::shared_ptr<ResourceController<std::unique_ptr<int>>> external;
  {
    auto parent = Scope::create("lifetime");
    external = parent->add<std::unique_ptr<int>>({
      "move-only", [](const ResourceContext&) { return std::make_unique<int>(23); },
      [&](std::unique_ptr<int>& v, const ResourceContext&) {
        assert(v && *v == 23); ++abandoned_release;
      }});
    assert(external->status() == ResourceStatus::idle);
  }
  // Parent auto-disposed; controller handle is safe even after scope destruction.
  assert(external->status() == ResourceStatus::released);
  fails([&] { (void)external->load(); }, "RESOURCE_RELEASED");
  assert(abandoned_release == 0);
  auto auto_scope = Scope::create("destructor");
  auto rr = auto_scope->add<std::unique_ptr<int>>({
    "moved", [](const ResourceContext&) { return std::make_unique<int>(19); },
    [&](std::unique_ptr<int>& ptr, const ResourceContext&) {
      assert(*ptr == 19); ++abandoned_release;
    }});
  assert(*rr->load() == 19);
  auto_scope.reset();
  assert(abandoned_release == 1 && rr->status() == ResourceStatus::released);
}
