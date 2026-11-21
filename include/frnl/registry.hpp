#pragma once
// Versioned Frnl service registry and deterministic dependency graph (C++17).
#include <algorithm>
#include <any>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace frnl {
struct Contract {
  std::string id;
  unsigned version = 0;
  std::vector<std::string> events, commands, capabilities;
};
struct Capability {
  std::string id;
  unsigned version = 0;
  std::vector<std::string> provides, requires_caps;
};
struct DependencyNode {
  std::string id;
  unsigned version = 1;
  std::vector<std::string> requires_caps, provides;
};
class DependencyGraph {
 public:
  void register_node(DependencyNode node) {
    if (node.id.empty() || node.version == 0)
      throw std::invalid_argument("invalid Frnl dependency node");
    if (nodes_.count(node.id)) throw std::invalid_argument("duplicate dependency node: " + node.id);
    for (const auto& key : node.provides)
      if (key.empty()) throw std::invalid_argument("empty provided capability");
    for (const auto& key : node.requires_caps)
      if (key.empty()) throw std::invalid_argument("empty required capability");
    const std::string id = node.id;
    nodes_.emplace(id, std::move(node));
    for (const auto& key : nodes_.at(id).provides) providers_[key].insert(id);
  }
  bool has(const std::string& id) const noexcept { return nodes_.count(id) != 0; }
  std::optional<std::string> resolve_dependency(const std::string& name) const {
    if (nodes_.count(name)) return name;
    const auto it = providers_.find(name);
    if (it == providers_.end() || it->second.empty()) return std::nullopt;
    return *it->second.begin(); // predictable lexical tie-break
  }
  std::vector<std::string> validate() const {
    std::vector<std::string> issues;
    for (const auto& node : nodes_)
      for (const auto& key : node.second.requires_caps)
        if (!resolve_dependency(key))
          issues.push_back(node.first + " requires missing dependency " + key);
    return issues;
  }
  std::vector<std::string> topological_order() const {
    std::map<std::string, int> marks;
    std::vector<std::string> order;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
      if (marks[id] == 1) throw std::logic_error("Frnl dependency cycle at: " + id);
      if (marks[id] == 2) return;
      marks[id] = 1;
      const auto& node = nodes_.at(id);
      for (const auto& needed : node.requires_caps) {
        const auto provider = resolve_dependency(needed);
        if (provider) visit(*provider);
      }
      marks[id] = 2;
      order.push_back(id);
    };
    for (const auto& node : nodes_) visit(node.first);
    return order;
  }
  void clear() noexcept { nodes_.clear(); providers_.clear(); }
 private:
  std::map<std::string, DependencyNode> nodes_;
  std::map<std::string, std::set<std::string>> providers_;
};
struct Registration {
  std::string id;
  unsigned version = 0;
  std::optional<Contract> contract;
  std::vector<Capability> capabilities;
  std::any value;
};
class Registry {
 public:
  void register_entry(Registration entry) {
    if (entry.id.empty() || entry.version == 0)
      throw std::invalid_argument("invalid Frnl registry version or ID");
    if (entry.contract &&
        (entry.contract->id.empty() || entry.contract->version == 0))
      throw std::invalid_argument("invalid Frnl contract");
    for (const auto& cap : entry.capabilities)
      if (cap.id.empty() || cap.version == 0)
        throw std::invalid_argument("invalid Frnl capability");
    auto it = entries_.find(entry.id);
    if (it != entries_.end() && it->second.version > entry.version)
      throw std::invalid_argument("cannot downgrade Frnl registry entry");
    entries_.insert_or_assign(entry.id, std::move(entry));
  }
  bool has(const std::string& id) const noexcept { return entries_.count(id) != 0; }
  const Registration& at(const std::string& id) const { return entries_.at(id); }
  template <class T> const T& resolve(const std::string& id) const {
    return std::any_cast<const T&>(at(id).value);
  }
  std::vector<std::string> ids() const {
    std::vector<std::string> result;
    for (const auto& entry : entries_) result.push_back(entry.first);
    return result;
  }
  void require_capabilities(const std::string& id, const std::set<std::string>& available) const {
    const auto& entry = at(id);
    if (entry.contract)
      for (const auto& name : entry.contract->capabilities)
        if (!available.count(name))
          throw std::invalid_argument("missing capability: " + name);
    for (const auto& cap : entry.capabilities)
      for (const auto& name : cap.requires_caps)
        if (!available.count(name))
          throw std::invalid_argument("missing capability: " + name);
  }
  DependencyGraph dependency_graph() const {
    DependencyGraph result;
    for (const auto& registration : entries_) {
      DependencyNode node;
      node.id = registration.first;
      node.version = registration.second.version;
      if (registration.second.contract)
        node.requires_caps = registration.second.contract->capabilities;
      for (const auto& cap : registration.second.capabilities) {
        node.requires_caps.insert(node.requires_caps.end(), cap.requires_caps.begin(), cap.requires_caps.end());
        node.provides.insert(node.provides.end(), cap.provides.begin(), cap.provides.end());
      }
      result.register_node(std::move(node));
    }
    return result;
  }
  void clear() noexcept { entries_.clear(); }
 private:
  std::map<std::string, Registration> entries_;
};
} // namespace frnl
