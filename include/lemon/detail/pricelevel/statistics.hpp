#pragma once

#include <atomic>
#include <cstdint>

namespace lemon {

struct PriceLevelStatistics {
  std::atomic<std::uint64_t> orders_added{0};
  std::atomic<std::uint64_t> orders_removed{0};
  std::atomic<std::uint64_t> orders_executed{0};
  std::atomic<std::uint64_t> quantity_executed{0};
  std::atomic<std::uint64_t> value_executed{0};
  std::atomic<std::uint64_t> last_execution_timestamp{0};
};

} // namespace lemon
