#pragma once
// External input adapters validate host/device payloads at the semantic event boundary.
// Parsing/AI resolution is supplied by the application. No platform runtime or fake model.
#include "semantic_runtime.hpp"
#include <any>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace frnl {
class InputAdapter {
 public:
  using Validator = std::function<bool(const std::any&)>;
  using Transform = std::function<std::any(const std::any&)>;
  explicit InputAdapter(SemanticRuntime& runtime) : runtime_(runtime) {}
  void bind(std::string input_id, std::vector<std::string> path,
            std::string event, Validator validator = {}, Transform transform = {}) {
    if (input_id.empty() || event.empty())
      throw std::invalid_argument("empty semantic input ID or event");
    (void)runtime_.component_at_path(path); // reject forged/unknown ancestry
    if (!bindings_.emplace(std::move(input_id), Binding{std::move(path),
                                                      std::move(event),
                                                      std::move(validator),
                                                      std::move(transform)}).second)
      throw std::invalid_argument("duplicate input binding");
  }
  bool unbind(const std::string& id) noexcept { return bindings_.erase(id) != 0; }
  std::any dispatch(const std::string& id, const std::any& raw) {
    const auto it = bindings_.find(id);
    if (it == bindings_.end()) throw std::invalid_argument("unknown input binding: " + id);
    const Binding binding = it->second; // safe under reentrant unbind
    const std::any payload = binding.transform ? binding.transform(raw) : raw;
    if (binding.validate && !binding.validate(payload))
      throw std::invalid_argument("semantic input failed validation: " + id);
    return runtime_.semantic_input(binding.path, binding.event, payload);
  }
 private:
  struct Binding {
    std::vector<std::string> path;
    std::string event;
    Validator validate;
    Transform transform;
  };
  SemanticRuntime& runtime_;
  std::map<std::string, Binding> bindings_;
};
} // namespace frnl
