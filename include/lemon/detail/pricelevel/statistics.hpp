#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <lemon/detail/errors.hpp>

namespace lemon {

struct PriceLevelStatisticsData {
  std::uint64_t orders_added{};
  std::uint64_t orders_removed{};
  std::uint64_t orders_executed{};
  std::uint64_t quantity_executed{};
  std::uint64_t value_executed{};
  std::uint64_t last_execution_timestamp{};
  std::uint64_t first_arrival_timestamp{};
  std::uint64_t sum_waiting_time{};
};

class PriceLevelStatistics {
public:
  PriceLevelStatistics()
    : first_arrival_timestamp_(current_timestamp_ms()) {
  }

  explicit PriceLevelStatistics(const PriceLevelStatisticsData& data)
    : orders_added_(data.orders_added),
      orders_removed_(data.orders_removed),
      orders_executed_(data.orders_executed),
      quantity_executed_(data.quantity_executed),
      value_executed_(data.value_executed),
      last_execution_timestamp_(data.last_execution_timestamp),
      first_arrival_timestamp_(data.first_arrival_timestamp),
      sum_waiting_time_(data.sum_waiting_time) {
  }

  PriceLevelStatistics(const PriceLevelStatistics& other)
    : PriceLevelStatistics(other.snapshot()) {
  }

  PriceLevelStatistics& operator=(const PriceLevelStatistics& other) {
    if (this != &other) {
      restore(other.snapshot());
    }
    return *this;
  }

  void record_order_added() {
    checked_increment(orders_added_, "orders_added");
  }

  void record_order_removed() {
    checked_increment(orders_removed_, "orders_removed");
  }

  void record_execution(
    std::uint64_t quantity,
    std::uint64_t price,
    std::uint64_t order_timestamp,
    std::uint64_t execution_timestamp
  ) {
    if (order_timestamp > execution_timestamp && order_timestamp != 0) {
      throw PriceLevelError("maker timestamp is later than execution timestamp");
    }
    if (quantity != 0 && price > std::numeric_limits<std::uint64_t>::max() / quantity) {
      throw PriceLevelError("executed value overflow");
    }

    const std::uint64_t value = price * quantity;
    const std::uint64_t waiting_time = order_timestamp == 0
      ? 0
      : execution_timestamp - order_timestamp;

    checked_increment(orders_executed_, "orders_executed");
    checked_add(quantity_executed_, quantity, "quantity_executed");
    checked_add(value_executed_, value, "value_executed");
    checked_add(sum_waiting_time_, waiting_time, "sum_waiting_time");
    last_execution_timestamp_.store(execution_timestamp, std::memory_order_relaxed);
  }

  std::uint64_t orders_added() const {
    return orders_added_.load(std::memory_order_relaxed);
  }

  std::uint64_t orders_removed() const {
    return orders_removed_.load(std::memory_order_relaxed);
  }

  std::uint64_t orders_executed() const {
    return orders_executed_.load(std::memory_order_relaxed);
  }

  std::uint64_t quantity_executed() const {
    return quantity_executed_.load(std::memory_order_relaxed);
  }

  std::uint64_t value_executed() const {
    return value_executed_.load(std::memory_order_relaxed);
  }

  std::uint64_t last_execution_timestamp() const {
    return last_execution_timestamp_.load(std::memory_order_relaxed);
  }

  std::uint64_t first_arrival_timestamp() const {
    return first_arrival_timestamp_.load(std::memory_order_relaxed);
  }

  std::uint64_t sum_waiting_time() const {
    return sum_waiting_time_.load(std::memory_order_relaxed);
  }

  std::optional<double> average_execution_price() const {
    const std::uint64_t quantity = quantity_executed();
    if (quantity == 0) {
      return std::nullopt;
    }
    return static_cast<double>(value_executed()) /
           static_cast<double>(quantity);
  }

  std::optional<double> average_waiting_time() const {
    const std::uint64_t count = orders_executed();
    if (count == 0) {
      return std::nullopt;
    }
    return static_cast<double>(sum_waiting_time()) /
           static_cast<double>(count);
  }

  std::optional<std::uint64_t> time_since_last_execution() const {
    const std::uint64_t last = last_execution_timestamp();
    if (last == 0) {
      return std::nullopt;
    }
    const std::uint64_t now = current_timestamp_ms();
    if (now < last) {
      return std::nullopt;
    }
    return now - last;
  }

  PriceLevelStatisticsData snapshot() const {
    return PriceLevelStatisticsData{
      orders_added(),
      orders_removed(),
      orders_executed(),
      quantity_executed(),
      value_executed(),
      last_execution_timestamp(),
      first_arrival_timestamp(),
      sum_waiting_time()
    };
  }

  void restore(const PriceLevelStatisticsData& data) {
    orders_added_.store(data.orders_added, std::memory_order_relaxed);
    orders_removed_.store(data.orders_removed, std::memory_order_relaxed);
    orders_executed_.store(data.orders_executed, std::memory_order_relaxed);
    quantity_executed_.store(data.quantity_executed, std::memory_order_relaxed);
    value_executed_.store(data.value_executed, std::memory_order_relaxed);
    last_execution_timestamp_.store(
      data.last_execution_timestamp,
      std::memory_order_relaxed
    );
    first_arrival_timestamp_.store(
      data.first_arrival_timestamp,
      std::memory_order_relaxed
    );
    sum_waiting_time_.store(data.sum_waiting_time, std::memory_order_relaxed);
  }

  void reset() {
    restore(PriceLevelStatisticsData{
      0, 0, 0, 0, 0, 0, current_timestamp_ms(), 0
    });
  }

private:
  static std::uint64_t current_timestamp_ms() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
      ).count()
    );
  }

  static void checked_increment(
    std::atomic<std::uint64_t>& target,
    const char* field
  ) {
    checked_add(target, 1, field);
  }

  static void checked_add(
    std::atomic<std::uint64_t>& target,
    std::uint64_t value,
    const char* field
  ) {
    std::uint64_t current = target.load(std::memory_order_relaxed);
    while (true) {
      if (value > std::numeric_limits<std::uint64_t>::max() - current) {
        throw PriceLevelError(std::string{field} + " overflow");
      }
      if (target.compare_exchange_weak(
            current,
            current + value,
            std::memory_order_relaxed,
            std::memory_order_relaxed
          )) {
        return;
      }
    }
  }

  std::atomic<std::uint64_t> orders_added_{0};
  std::atomic<std::uint64_t> orders_removed_{0};
  std::atomic<std::uint64_t> orders_executed_{0};
  std::atomic<std::uint64_t> quantity_executed_{0};
  std::atomic<std::uint64_t> value_executed_{0};
  std::atomic<std::uint64_t> last_execution_timestamp_{0};
  std::atomic<std::uint64_t> first_arrival_timestamp_{0};
  std::atomic<std::uint64_t> sum_waiting_time_{0};
};

} // namespace lemon
