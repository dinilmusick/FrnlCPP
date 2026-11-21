#pragma once
// FrnlTS core fault and typed command contracts. No graphics or platform code.
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace frnl {
class Fault : public std::runtime_error {
 public:
  Fault(std::string code, std::string message, bool recoverable = false,
        bool retryable = false, std::exception_ptr cause = {})
      : std::runtime_error(std::move(message)), code_(std::move(code)),
        recoverable_(recoverable), retryable_(retryable), cause_(std::move(cause)) {
    if (code_.empty()) throw std::invalid_argument("Frnl fault code must not be empty");
  }
  const std::string& code() const noexcept { return code_; }
  bool recoverable() const noexcept { return recoverable_; }
  bool retryable() const noexcept { return retryable_; }
  const std::exception_ptr& cause() const noexcept { return cause_; }

 private:
  std::string code_;
  bool recoverable_ = false;
  bool retryable_ = false;
  std::exception_ptr cause_;
};

template <class T>
struct Command {
  std::string id;
  std::optional<T> payload;
};
template <class T>
Command<T> define_command(std::string id, std::optional<T> payload = {}) {
  if (id.empty()) throw std::invalid_argument("Frnl command ID must not be empty");
  return {std::move(id), std::move(payload)};
}
} // namespace frnl
