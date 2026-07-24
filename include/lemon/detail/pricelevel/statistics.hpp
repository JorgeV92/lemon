#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
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
  bool stats_degraded{};
};

class PriceLevelStatistics {
public:
  PriceLevelStatistics()
    : first_arrival_timestamp_(current_timestamp_ms()) {
  }

  explicit PriceLevelStatistics(const PriceLevelStatisticsData& data) {
    restore(data);
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
    std::lock_guard<std::mutex> lock(mutex_);
    if (orders_added_ == std::numeric_limits<std::uint64_t>::max()) {
      stats_degraded_ = true;
      return;
    }
    ++orders_added_;
  }

  void record_order_removed() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (orders_removed_ == std::numeric_limits<std::uint64_t>::max()) {
      stats_degraded_ = true;
      return;
    }
    ++orders_removed_;
  }

  void record_execution(
    std::uint64_t quantity,
    std::uint64_t price,
    std::uint64_t order_timestamp,
    std::uint64_t execution_timestamp
  ) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
      if (order_timestamp > execution_timestamp && order_timestamp != 0) {
        throw PriceLevelError(
          "maker timestamp is later than execution timestamp"
        );
      }
      if (quantity != 0 &&
          price > std::numeric_limits<std::uint64_t>::max() / quantity) {
        throw PriceLevelError("executed value overflow");
      }

      const std::uint64_t value = price * quantity;
      const std::uint64_t waiting_time = order_timestamp == 0
        ? 0
        : execution_timestamp - order_timestamp;

      // Derive every field first. No statistics field changes unless all
      // checked arithmetic succeeds.
      const std::uint64_t orders_executed = checked_sum(
        orders_executed_, 1, "orders_executed"
      );
      const std::uint64_t quantity_executed = checked_sum(
        quantity_executed_, quantity, "quantity_executed"
      );
      const std::uint64_t value_executed = checked_sum(
        value_executed_, value, "value_executed"
      );
      const std::uint64_t sum_waiting_time = checked_sum(
        sum_waiting_time_, waiting_time, "sum_waiting_time"
      );

      orders_executed_ = orders_executed;
      quantity_executed_ = quantity_executed;
      value_executed_ = value_executed;
      sum_waiting_time_ = sum_waiting_time;
      last_execution_timestamp_ = std::max(
        last_execution_timestamp_,
        execution_timestamp
      );
    } catch (const PriceLevelError&) {
      stats_degraded_ = true;
      throw;
    }
  }

  std::uint64_t orders_added() const {
    return snapshot().orders_added;
  }

  std::uint64_t orders_removed() const {
    return snapshot().orders_removed;
  }

  std::uint64_t orders_executed() const {
    return snapshot().orders_executed;
  }

  std::uint64_t quantity_executed() const {
    return snapshot().quantity_executed;
  }

  std::uint64_t value_executed() const {
    return snapshot().value_executed;
  }

  std::uint64_t last_execution_timestamp() const {
    return snapshot().last_execution_timestamp;
  }

  std::uint64_t first_arrival_timestamp() const {
    return snapshot().first_arrival_timestamp;
  }

  std::uint64_t sum_waiting_time() const {
    return snapshot().sum_waiting_time;
  }

  bool stats_degraded() const {
    return snapshot().stats_degraded;
  }

  std::optional<double> average_execution_price() const {
    const PriceLevelStatisticsData data = snapshot();
    if (data.quantity_executed == 0) {
      return std::nullopt;
    }
    return static_cast<double>(data.value_executed) /
           static_cast<double>(data.quantity_executed);
  }

  std::optional<double> average_waiting_time() const {
    const PriceLevelStatisticsData data = snapshot();
    if (data.orders_executed == 0) {
      return std::nullopt;
    }
    return static_cast<double>(data.sum_waiting_time) /
           static_cast<double>(data.orders_executed);
  }

  std::optional<std::uint64_t> time_since_last_execution() const {
    const std::uint64_t last = snapshot().last_execution_timestamp;
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
    std::lock_guard<std::mutex> lock(mutex_);
    return PriceLevelStatisticsData{
      orders_added_,
      orders_removed_,
      orders_executed_,
      quantity_executed_,
      value_executed_,
      last_execution_timestamp_,
      first_arrival_timestamp_,
      sum_waiting_time_,
      stats_degraded_
    };
  }

  void restore(const PriceLevelStatisticsData& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    orders_added_ = data.orders_added;
    orders_removed_ = data.orders_removed;
    orders_executed_ = data.orders_executed;
    quantity_executed_ = data.quantity_executed;
    value_executed_ = data.value_executed;
    last_execution_timestamp_ = data.last_execution_timestamp;
    first_arrival_timestamp_ = data.first_arrival_timestamp;
    sum_waiting_time_ = data.sum_waiting_time;
    stats_degraded_ = data.stats_degraded;
  }

  void reset() {
    restore(PriceLevelStatisticsData{
      0, 0, 0, 0, 0, 0, current_timestamp_ms(), 0, false
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

  static std::uint64_t checked_sum(
    std::uint64_t current,
    std::uint64_t value,
    const char* field
  ) {
    if (value > std::numeric_limits<std::uint64_t>::max() - current) {
      throw PriceLevelError(std::string{field} + " overflow");
    }
    return current + value;
  }

  mutable std::mutex mutex_;
  std::uint64_t orders_added_{};
  std::uint64_t orders_removed_{};
  std::uint64_t orders_executed_{};
  std::uint64_t quantity_executed_{};
  std::uint64_t value_executed_{};
  std::uint64_t last_execution_timestamp_{};
  std::uint64_t first_arrival_timestamp_{};
  std::uint64_t sum_waiting_time_{};
  bool stats_degraded_{};
};

} // namespace lemon
