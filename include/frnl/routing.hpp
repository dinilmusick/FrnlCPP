#pragma once
// Pure native route matching/building; no HTTP client, browser or router process.
#include <algorithm>
#include <any>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace frnl {
struct Route {
  std::string id, pattern;
  std::function<std::any()> load;
};
struct RouteMatch {
  std::string id, path;
  std::map<std::string, std::string> params, query;
};
struct RouteBuildOptions {
  bool strict = false;
  bool append_unmatched_to_query = false;
};
inline std::string normalize_route_pattern(std::string pattern) {
  const auto first = pattern.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "/";
  const auto last = pattern.find_last_not_of(" \t\r\n");
  pattern = pattern.substr(first, last - first + 1);
  if (pattern.front() != '/') pattern.insert(pattern.begin(), '/');
  if (pattern.size() > 1 && pattern.back() == '/') pattern.pop_back();
  return pattern;
}
inline std::vector<std::string> route_segments(const std::string& path) {
  std::vector<std::string> result;
  std::size_t begin = 0;
  for (;;) {
    const auto end = path.find('/', begin);
    if (end == std::string::npos) { result.push_back(path.substr(begin)); break; }
    result.push_back(path.substr(begin, end - begin));
    begin = end + 1;
  }
  return result;
}
inline int hex_digit(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}
inline std::string uri_decode(const std::string& input, bool plus_as_space = false) {
  std::string result;
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '+' && plus_as_space) { result += ' '; continue; }
    if (input[i] != '%') { result += input[i]; continue; }
    if (i + 2 >= input.size()) throw std::invalid_argument("bad percent-encoded URL");
    const int a = hex_digit(input[i + 1]), b = hex_digit(input[i + 2]);
    if (a < 0 || b < 0) throw std::invalid_argument("bad percent-encoded URL");
    result += static_cast<char>((a << 4) | b);
    i += 2;
  }
  return result;
}
inline std::string uri_encode(const std::string& input, bool query = false) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char value : input) {
    if (std::isalnum(value) || value == '-' || value == '_' || value == '.' || value == '~')
      out += static_cast<char>(value);
    else if (value == ' ' && query) out += '+';
    else {
      out += '%';
      out += digits[value >> 4];
      out += digits[value & 15];
    }
  }
  return out;
}
inline std::pair<std::string, std::map<std::string, std::string>>
parse_route_url(std::string url) {
  const auto scheme = url.find("://");
  if (scheme != std::string::npos) {
    const auto boundary = url.find_first_of("/?#", scheme + 3);
    if (boundary == std::string::npos) url = "/";
    else url = url[boundary] == '/' ? url.substr(boundary) : "/" + url.substr(boundary);
  }
  const auto hash = url.find('#');
  if (hash != std::string::npos) url.resize(hash);
  const auto qmark = url.find('?');
  std::string query = qmark == std::string::npos ? "" : url.substr(qmark + 1);
  std::string path = qmark == std::string::npos ? url : url.substr(0, qmark);
  if (path.empty()) path = "/";
  std::map<std::string, std::string> params;
  std::size_t offset = 0;
  while (offset < query.size()) {
    const auto amp = query.find('&', offset);
    const auto pair = query.substr(offset, amp == std::string::npos ? std::string::npos : amp - offset);
    if (!pair.empty()) {
      const auto equal = pair.find('=');
      const auto key = uri_decode(pair.substr(0, equal), true);
      const auto val = equal == std::string::npos ? "" : uri_decode(pair.substr(equal + 1), true);
      params[key] = val;
    }
    if (amp == std::string::npos) break;
    offset = amp + 1;
  }
  return {path, params};
}
inline std::optional<RouteMatch> match_route(const Route& route, const std::string& url) {
  const auto parsed = parse_route_url(url);
  std::string path = parsed.first;
  if (path.size() > 1 && path.back() == '/') path.pop_back();
  const auto patterns = route_segments(normalize_route_pattern(route.pattern));
  const auto values = route_segments(path);
  std::map<std::string, std::string> captures;
  std::size_t index = 0;
  for (; index < patterns.size(); ++index) {
    const auto& part = patterns[index];
    if (!part.empty() && part.front() == '*') {
      const auto name = part.size() > 1 ? part.substr(1) : std::string("wildcard");
      if (index != patterns.size() - 1) throw std::invalid_argument("route wildcard must be final");
      std::string rest;
      for (std::size_t i = index; i < values.size(); ++i) {
        if (i != index) rest += '/';
        rest += values[i];
      }
      captures[name] = uri_decode(rest);
      index = patterns.size();
      break;
    }
    if (index >= values.size()) return std::nullopt;
    if (!part.empty() && part.front() == ':') {
      if (part.size() == 1 || values[index].empty()) throw std::invalid_argument("invalid route parameter");
      captures[part.substr(1)] = uri_decode(values[index]);
    } else if (part != values[index]) return std::nullopt;
  }
  if (index != patterns.size() || (values.size() > patterns.size() && (patterns.empty() || patterns.back().empty() || patterns.back().front() != '*')))
    return std::nullopt;
  return RouteMatch{route.id, path, std::move(captures), parsed.second};
}
inline std::string build_route_url(
    const Route& route, const std::map<std::string, std::string>& params = {},
    std::map<std::string, std::string> query = {}, RouteBuildOptions options = {}) {
  const auto parts = route_segments(normalize_route_pattern(route.pattern));
  std::set<std::string> consumed;
  std::string output = "/";
  for (std::size_t part_index = 1; part_index < parts.size(); ++part_index) {
    const auto& part = parts[part_index];
    if (part_index > 1) output += '/';
    if (!part.empty() && part.front() == ':') {
      const auto name = part.substr(1);
      const auto p = params.find(name);
      if (p == params.end() || p->second.empty())
        throw std::invalid_argument("missing required route parameter: " + name);
      consumed.insert(name);
      output += uri_encode(p->second);
    } else if (!part.empty() && part.front() == '*') {
      const auto name = part.size() > 1 ? part.substr(1) : std::string("wildcard");
      const auto p = params.find(name);
      consumed.insert(name);
      consumed.insert("wildcard");
      const std::string value = p == params.end() ? "" : p->second;
      const auto tokens = route_segments(value);
      for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i) output += '/';
        output += uri_encode(tokens[i]);
      }
    } else output += part;
  }
  for (const auto& param : params) {
    if (consumed.count(param.first)) continue;
    if (options.strict) throw std::invalid_argument("unknown route parameter: " + param.first);
    if (options.append_unmatched_to_query && !query.count(param.first))
      query.emplace(param.first, param.second);
  }
  if (output.empty()) output = "/";
  bool first = true;
  for (const auto& value : query) {
    output += first ? '?' : '&';
    first = false;
    output += uri_encode(value.first, true) + '=' + uri_encode(value.second, true);
  }
  return output;
}
class Router {
 public:
  void add_route(Route route) {
    if (route.id.empty()) throw std::invalid_argument("empty route ID");
    if (routes_.count(route.id)) throw std::invalid_argument("duplicate route ID");
    const auto pattern = normalize_route_pattern(route.pattern);
    if (patterns_.count(pattern)) throw std::invalid_argument("duplicate route pattern");
    const auto id = route.id;
    route.pattern = pattern;
    routes_.emplace(id, std::move(route));
    patterns_.insert(pattern);
    order_.push_back(id);
  }
  const Route& route(const std::string& id) const { return routes_.at(id); }
  std::optional<RouteMatch> match(const std::string& url) const {
    for (const auto& id : order_) {
      const auto result = match_route(routes_.at(id), url);
      if (result) return result;
    }
    return std::nullopt;
  }
  std::pair<std::optional<RouteMatch>, std::any> resolve(const std::string& url) const {
    const auto result = match(url);
    if (!result) return {std::nullopt, {}};
    const auto& found = route(result->id);
    return {result, found.load ? found.load() : std::any{}};
  }
  void clear() noexcept { routes_.clear(); patterns_.clear(); order_.clear(); }
 private:
  std::map<std::string, Route> routes_;
  std::set<std::string> patterns_;
  std::vector<std::string> order_;
};
} // namespace frnl
