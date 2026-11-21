#pragma once
// Portable FrnlTS lifecycle counterpart. Transition notifications are snapshot-safe.
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
namespace frnl {
enum class LifecyclePhase {
  created, initializing, mounted, active, suspended,
  resuming, disposing, disposed, failed
};
inline bool can_transition(LifecyclePhase from, LifecyclePhase to) noexcept {
  using P = LifecyclePhase;
  switch (from) {
    case P::created: return to == P::initializing || to == P::disposing;
    case P::initializing: return to == P::mounted || to == P::failed || to == P::disposing;
    case P::mounted: return to == P::active || to == P::suspended || to == P::disposing || to == P::failed;
    case P::active: return to == P::suspended || to == P::disposing || to == P::failed;
    case P::suspended: return to == P::resuming || to == P::disposing || to == P::failed;
    case P::resuming: return to == P::active || to == P::failed || to == P::disposing;
    case P::disposing: return to == P::disposed;
    case P::disposed: return false;
    case P::failed: return to == P::initializing || to == P::disposing || to == P::disposed;
  }
  return false;
}
struct LifecycleTransition {
  LifecyclePhase from;
  LifecyclePhase to;
};
struct LifecycleListenerStore {
  std::map<std::size_t, std::function<void(const LifecycleTransition&)>> listeners;
  std::size_t next = 1;
};
class LifecycleSubscription {
 public:
  LifecycleSubscription() = default;
  LifecycleSubscription(std::weak_ptr<LifecycleListenerStore> s, std::size_t id)
      : store_(std::move(s)), id_(id) {}
  LifecycleSubscription(const LifecycleSubscription&) = delete;
  LifecycleSubscription& operator=(const LifecycleSubscription&) = delete;
  LifecycleSubscription(LifecycleSubscription&& o) noexcept
      : store_(std::move(o.store_)), id_(o.id_) { o.id_ = 0; }
  LifecycleSubscription& operator=(LifecycleSubscription&& o) noexcept {
    if (this != &o) { reset(); store_ = std::move(o.store_); id_ = o.id_; o.id_ = 0; }
    return *this;
  }
  ~LifecycleSubscription() { reset(); }
  void reset() noexcept {
    if (auto store = store_.lock()) store->listeners.erase(id_);
    store_.reset(); id_ = 0;
  }
 private:
  std::weak_ptr<LifecycleListenerStore> store_;
  std::size_t id_ = 0;
};
class Lifecycle {
 public:
  using Listener = std::function<void(const LifecycleTransition&)>;
  explicit Lifecycle(LifecyclePhase initial = LifecyclePhase::created)
      : phase_(initial), listeners_(std::make_shared<LifecycleListenerStore>()) {}
  Lifecycle(const Lifecycle&) = delete;
  Lifecycle& operator=(const Lifecycle&) = delete;
  LifecyclePhase phase() const noexcept { return phase_; }
  void transition(LifecyclePhase next) {
    if (next == phase_) return;
    if (!can_transition(phase_, next)) throw std::logic_error("invalid Frnl lifecycle transition");
    const LifecycleTransition notice{phase_, next};
    phase_ = next;
    const auto snapshot = listeners_->listeners;
    for (const auto& listener : snapshot) listener.second(notice);
  }
  LifecycleSubscription subscribe(Listener listener) {
    if (!listener) throw std::invalid_argument("empty lifecycle listener");
    const auto id = listeners_->next++;
    listeners_->listeners.emplace(id, std::move(listener));
    return LifecycleSubscription{listeners_, id};
  }
 private:
  LifecyclePhase phase_;
  std::shared_ptr<LifecycleListenerStore> listeners_;
};
} // namespace frnl
