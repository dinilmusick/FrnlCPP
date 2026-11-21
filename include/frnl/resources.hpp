#pragma once
// Synchronous, platform-neutral resource contracts at pinned FrnlTS resources.ts.
// Async work must be supplied by a host adapter: this kernel does not start threads.
#include "fault.hpp"
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace frnl {
class CancellationToken {
 public:
  CancellationToken() : stopped_(std::make_shared<std::atomic<bool>>(false)) {}
  bool aborted() const noexcept { return stopped_->load(std::memory_order_acquire); }
  void cancel() const noexcept { stopped_->store(true, std::memory_order_release); }
 private:
  std::shared_ptr<std::atomic<bool>> stopped_;
};

struct ResourceContext {
  CancellationToken signal;
  std::string scope_id;
};
enum class ResourceStatus { idle, loading, ready, error, released };

template <class T>
struct Resource {
  std::string id;
  std::function<T(const ResourceContext&)> acquire;
  std::function<void(T&, const ResourceContext&)> release;
};
template <class T>
Resource<T> define_resource(Resource<T> resource) {
  if (resource.id.empty() || !resource.acquire)
    throw std::invalid_argument("Frnl resource needs an ID and acquire callback");
  return resource;
}
template <class T>
struct Effect {
  std::string id;
  std::function<T(const CancellationToken&)> run;
};
template <class T>
Effect<T> define_effect(std::string id, std::function<T(const CancellationToken&)> fn) {
  if (id.empty() || !fn) throw std::invalid_argument("invalid Frnl effect");
  return {std::move(id), std::move(fn)};
}

class ResourceBase {
 public:
  virtual ~ResourceBase() = default;
  virtual void release() = 0;
};
template <class T>
class ResourceController final : public ResourceBase {
 public:
  ResourceController(Resource<T> resource, std::string scope_id, CancellationToken scope_signal)
      : resource_(define_resource(std::move(resource))),
        context_{std::move(scope_signal), std::move(scope_id)} {}
  ResourceController(const ResourceController&) = delete;
  ResourceController& operator=(const ResourceController&) = delete;
  ~ResourceController() override {
    try { release(); } catch (...) {} // explicit release() reports errors
  }
  const std::string& id() const noexcept { return resource_.id; }
  ResourceStatus status() const noexcept { return status_; }
  const std::optional<Fault>& error() const noexcept { return error_; }
  const std::optional<T>& value() const noexcept { return value_; }
  const ResourceContext& context() const noexcept { return context_; }

  const T& load() {
    if (status_ == ResourceStatus::ready) return *value_;
    if (status_ == ResourceStatus::released)
      throw Fault("RESOURCE_RELEASED", "Resource already released: " + id());
    if (context_.signal.aborted())
      throw Fault("RESOURCE_ABORTED", "Resource acquisition aborted: " + id());
    if (status_ == ResourceStatus::loading)
      throw Fault("RESOURCE_REENTRANT_LOAD", "Reentrant synchronous resource acquisition: " + id());
    status_ = ResourceStatus::loading;
    error_.reset();
    try {
      T acquired = resource_.acquire(context_);
      if (context_.signal.aborted() || status_ == ResourceStatus::released) {
        if (resource_.release) {
          try { resource_.release(acquired, context_); } catch (...) {}
        }
        throw Fault("RESOURCE_ABORTED", "Resource acquired after release: " + id());
      }
      value_.emplace(std::move(acquired));
      status_ = ResourceStatus::ready;
      return *value_;
    } catch (const Fault& error) {
      if (status_ != ResourceStatus::released) {
        error_ = error;
        status_ = ResourceStatus::error;
      }
      throw;
    } catch (...) {
      if (status_ == ResourceStatus::released)
        throw Fault("RESOURCE_ABORTED", "Resource released while acquiring: " + id());
      const auto cause = std::current_exception();
      try { std::rethrow_exception(cause); }
      catch (const std::exception& e) {
        error_.emplace("RESOURCE_ACQUIRE_FAILED", e.what(), true, true, cause);
      } catch (...) {
        error_.emplace("RESOURCE_ACQUIRE_FAILED", "Unknown resource error", true, true, cause);
      }
      status_ = ResourceStatus::error;
      throw *error_;
    }
  }
  void release() override {
    if (status_ == ResourceStatus::released) return;
    status_ = ResourceStatus::released;
    context_.signal.cancel();
    auto old = std::move(value_);
    value_.reset();
    if (old && resource_.release) {
      try { resource_.release(*old, context_); }
      catch (...) {
        const auto cause = std::current_exception();
        try { std::rethrow_exception(cause); }
        catch (const std::exception& e) {
          error_.emplace("RESOURCE_RELEASE_FAILED", e.what(), false, false, cause);
        } catch (...) {
          error_.emplace("RESOURCE_RELEASE_FAILED", "Unknown release error", false, false, cause);
        }
        throw *error_;
      }
    }
  }
 private:
  Resource<T> resource_;
  ResourceContext context_;
  ResourceStatus status_ = ResourceStatus::idle;
  std::optional<T> value_;
  std::optional<Fault> error_;
};

class Scope final : public std::enable_shared_from_this<Scope> {
 public:
  static std::shared_ptr<Scope> create(std::string id) {
    if (id.empty()) throw std::invalid_argument("empty scope ID");
    return std::shared_ptr<Scope>(new Scope(std::move(id)));
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
  ~Scope() noexcept {
    try { dispose(); } catch (...) {}
  }
  const std::string& id() const noexcept { return id_; }
  bool disposed() const noexcept { return disposed_; }
  CancellationToken signal() const noexcept { return signal_; }
  std::shared_ptr<Scope> create_child(std::string id) {
    ensure_alive();
    if (id.empty()) throw std::invalid_argument("empty child scope ID");
    auto child = Scope::create(std::move(id));
    child->parent_ = shared_from_this();
    children_.push_back(child);
    return child;
  }
  template <class T>
  std::shared_ptr<ResourceController<T>> add(Resource<T> resource) {
    ensure_alive();
    auto controller = std::make_shared<ResourceController<T>>(
        std::move(resource), id_, signal_);
    resources_.push_back(controller);
    return controller;
  }
  void dispose() {
    if (disposed_) return;
    disposed_ = true; // fence reentrant add/create before callbacks
    signal_.cancel();
    std::exception_ptr first_error;
    for (auto& child : children_) {
      try { child->dispose(); } catch (...) { if (!first_error) first_error = std::current_exception(); }
    }
    children_.clear();
    for (auto& resource : resources_) {
      try { resource->release(); } catch (...) { if (!first_error) first_error = std::current_exception(); }
    }
    resources_.clear();
    if (first_error) std::rethrow_exception(first_error);
  }
 private:
  explicit Scope(std::string id) : id_(std::move(id)) {}
  void ensure_alive() const {
    if (disposed_ || signal_.aborted())
      throw Fault("SCOPE_DISPOSED", "Frnl scope is disposed: " + id_);
  }
  std::string id_;
  bool disposed_ = false;
  CancellationToken signal_;
  std::weak_ptr<Scope> parent_;
  std::vector<std::shared_ptr<Scope>> children_;
  std::vector<std::shared_ptr<ResourceBase>> resources_;
};
} // namespace frnl
