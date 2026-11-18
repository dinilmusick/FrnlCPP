#pragma once
#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
namespace frnlcpp {
// Headless counterpart to FrnlTS component parent addressing.
class ComponentScope {
 public:
  void register_component(std::string id, std::string parent = {}) {
    if (id.empty()) throw std::invalid_argument("Frnl component id cannot be empty");
    if (parent == id) throw std::invalid_argument("Circular Frnl component scope detected at: " + id);
    parents_[std::move(id)] = std::move(parent);
  }
  std::vector<std::string> resolve_path(const std::string& component_id) const {
    if (component_id.empty() || parents_.find(component_id) == parents_.end())
      throw std::invalid_argument("Unknown Frnl component: " + component_id);
    std::vector<std::string> path;
    std::unordered_set<std::string> seen;
    std::string cursor = component_id;
    while (!cursor.empty()) {
      if (!seen.insert(cursor).second)
        throw std::logic_error("Circular Frnl component scope detected at: " + cursor);
      const auto it = parents_.find(cursor);
      if (it == parents_.end())
        throw std::invalid_argument("Unknown Frnl parent component: " + cursor);
      path.push_back(cursor);
      cursor = it->second;
    }
    std::reverse(path.begin(), path.end());
    return path;
  }
  // Reject stale or forged ancestry rather than resolving by leaf alone.
  std::string component_at_path(const std::vector<std::string>& path) const {
    if (path.empty()) throw std::invalid_argument("Frnl component path cannot be empty");
    if (resolve_path(path.back()) != path)
      throw std::invalid_argument("Frnl component path does not match registered scope");
    return path.back();
  }
 private:
  std::unordered_map<std::string, std::string> parents_;
};
}  // namespace frnlcpp
