#pragma once
// Renderer-neutral Frnl slots: occupant, own properties and repeatable collection
// changes remain distinct from the occupant component's state and identity.
#include "semantic_runtime.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace frnl {
enum class SlotEventType { state_changed, occupant_changed, property_changed, collection_changed };
struct SlotEvent {
  SlotEventType type;
  std::string owner, slot;
  std::string occupant;
  std::string key;
  std::string state;
};
struct SlotState {
  bool repeatable = false;
  std::string occupant;
  Properties properties;
  std::vector<std::string> collection;
};
struct SlotStore {
  using Listener = std::function<void(const SlotEvent&)>;
  std::map<std::string, SlotState> slots;
  std::map<std::size_t, Listener> listeners;
  std::size_t next = 1;
};
class SlotSubscription {
 public:
  SlotSubscription() = default;
  SlotSubscription(std::weak_ptr<SlotStore> store, std::size_t id)
      : store_(std::move(store)), id_(id) {}
  SlotSubscription(const SlotSubscription&) = delete;
  SlotSubscription& operator=(const SlotSubscription&) = delete;
  SlotSubscription(SlotSubscription&& other) noexcept
      : store_(std::move(other.store_)), id_(other.id_) { other.id_ = 0; }
  SlotSubscription& operator=(SlotSubscription&& other) noexcept {
    if (this != &other) { reset(); store_ = std::move(other.store_); id_ = other.id_; other.id_ = 0; }
    return *this;
  }
  ~SlotSubscription() { reset(); }
  void reset() noexcept {
    if (auto s = store_.lock()) s->listeners.erase(id_);
    store_.reset(); id_ = 0;
  }
 private:
  std::weak_ptr<SlotStore> store_;
  std::size_t id_ = 0;
};
class SlotModel {
 public:
  explicit SlotModel(SemanticRuntime& runtime)
      : runtime_(runtime), state_(std::make_shared<SlotStore>()) {
    std::weak_ptr<SlotStore> weak = state_;
    update_token_ = runtime.on_update([weak](const Update& update) {
      if (update.kind != "state") return;
      auto state = weak.lock();
      if (!state) return;
      std::vector<SlotEvent> changes;
      for (const auto& slot : state->slots) {
        const auto separator = slot.first.find('\n');
        const auto owner = slot.first.substr(0, separator);
        const auto name = slot.first.substr(separator + 1);
        const bool direct = slot.second.occupant == update.component_id;
        const bool collected = std::find(slot.second.collection.begin(),
          slot.second.collection.end(), update.component_id) != slot.second.collection.end();
        if (direct || collected)
          changes.push_back({SlotEventType::state_changed, owner, name,
                             update.component_id, {}, update.state});
      }
      for (const auto& change : changes) publish(*state, change);
    });
  }
  SlotModel(const SlotModel&) = delete;
  SlotModel& operator=(const SlotModel&) = delete;
  ~SlotModel() = default;
  void define_slot(const std::string& owner, const std::string& name, bool repeatable = false) {
    if (!runtime_.has_component(owner)) throw std::invalid_argument("unknown slot owner");
    if (name.empty() || name.find('\n') != std::string::npos || owner.find('\n') != std::string::npos)
      throw std::invalid_argument("invalid slot name or owner");
    if (!state_->slots.emplace(key(owner, name), SlotState{repeatable, {}, {}, {}}).second)
      throw std::invalid_argument("duplicate slot");
  }
  const SlotState& at(const std::string& owner, const std::string& name) const {
    return state_->slots.at(key(owner, name));
  }
  void set_occupant(const std::string& owner, const std::string& name,
                    const std::string& occupant) {
    if (!occupant.empty() && !runtime_.has_component(occupant))
      throw std::invalid_argument("unknown slot occupant");
    auto& slot = state_->slots.at(key(owner, name));
    if (slot.occupant == occupant) return;
    slot.occupant = occupant;
    publish(*state_, {SlotEventType::occupant_changed, owner, name, occupant, {}, {}});
  }
  void set_property(const std::string& owner, const std::string& name,
                    std::string property, Value value) {
    if (property.empty()) throw std::invalid_argument("empty slot property");
    const auto property_key = property;
    state_->slots.at(key(owner, name)).properties.insert_or_assign(std::move(property), std::move(value));
    publish(*state_, {SlotEventType::property_changed, owner, name, {}, property_key, {}});
  }
  void append(const std::string& owner, const std::string& name,
              const std::string& component) {
    if (!runtime_.has_component(component)) throw std::invalid_argument("unknown collection occupant");
    auto& slot = state_->slots.at(key(owner, name));
    if (!slot.repeatable) throw std::invalid_argument("non-repeatable slot");
    if (std::find(slot.collection.begin(), slot.collection.end(), component) != slot.collection.end())
      throw std::invalid_argument("duplicate collection member");
    slot.collection.push_back(component);
    publish(*state_, {SlotEventType::collection_changed, owner, name, component, {}, {}});
  }
  bool remove(const std::string& owner, const std::string& name,
              const std::string& component) {
    auto& values = state_->slots.at(key(owner, name)).collection;
    const auto it = std::find(values.begin(), values.end(), component);
    if (it == values.end()) return false;
    values.erase(it);
    publish(*state_, {SlotEventType::collection_changed, owner, name, component, {}, {}});
    return true;
  }
  SlotSubscription subscribe(SlotStore::Listener callback) {
    if (!callback) throw std::invalid_argument("empty slot listener");
    const auto id = state_->next++;
    state_->listeners.emplace(id, std::move(callback));
    return SlotSubscription{state_, id};
  }
 private:
  static std::string key(const std::string& owner, const std::string& name) {
    return owner + "\n" + name;
  }
  static void publish(SlotStore& state, const SlotEvent& event) {
    const auto snapshot = state.listeners;
    for (const auto& listener : snapshot) listener.second(event);
  }
  SemanticRuntime& runtime_;
  std::shared_ptr<SlotStore> state_;
  Subscription update_token_;
};
} // namespace frnl
