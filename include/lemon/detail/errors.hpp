#pragma once

#include <stdexcept>
#include <string>

namespace lemon {

class PriceLevelError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct InvalidOperation {
  std::string message;
};

struct SerializationError {
  std::string message;
};

struct DeserializationError {
  std::string message;
};

struct ChecksumMismatch {
  std::string expected;
  std::string actual;
};

} // namespace lemon
