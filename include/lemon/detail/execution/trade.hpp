#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <lemon/detail/errors.hpp>
#include <lemon/detail/orders/order_type.hpp>
#include <lemon/utils/value.hpp>

namespace lemon {

using TradeId = std::uint64_t;

class TradeIdGenerator {
public:
  explicit TradeIdGenerator(TradeId initial_value = 1)
    : next_id_(initial_value) {
  }

  TradeId next() {
    TradeId current = next_id_.load(std::memory_order_relaxed);
    while (true) {
      if (current == std::numeric_limits<TradeId>::max()) {
        throw PriceLevelError("trade ID sequence exhausted");
      }
      if (next_id_.compare_exchange_weak(
            current,
            current + 1,
            std::memory_order_relaxed,
            std::memory_order_relaxed
          )) {
        return current;
      }
    }
  }

private:
  std::atomic<TradeId> next_id_;
};

class Trade {
public:
  Trade() = default;

  Trade(
    TradeId trade_id,
    OrderId taker_order_id,
    OrderId maker_order_id,
    Price price,
    Quantity quantity,
    Side taker_side
  )
    : Trade(
        trade_id,
        taker_order_id,
        maker_order_id,
        price,
        quantity,
        taker_side,
        current_timestamp_ms()
      ) {
  }

  Trade(
    TradeId trade_id,
    OrderId taker_order_id,
    OrderId maker_order_id,
    Price price,
    Quantity quantity,
    Side taker_side,
    TimestampMs timestamp
  )
    : trade_id_(trade_id),
      taker_order_id_(taker_order_id),
      maker_order_id_(maker_order_id),
      price_(price),
      quantity_(quantity),
      taker_side_(taker_side),
      timestamp_(timestamp) {
  }

  TradeId trade_id() const {
    return trade_id_;
  }

  OrderId taker_order_id() const {
    return taker_order_id_;
  }

  OrderId maker_order_id() const {
    return maker_order_id_;
  }

  Price price() const {
    return price_;
  }

  Quantity quantity() const {
    return quantity_;
  }

  Side taker_side() const {
    return taker_side_;
  }

  TimestampMs timestamp() const {
    return timestamp_;
  }

  Side maker_side() const {
    return taker_side_ == Side::Buy ? Side::Sell : Side::Buy;
  }

  value_type total_value() const {
    const value_type price = price_.value();
    const value_type quantity = quantity_.value();
    if (quantity != 0 && price > std::numeric_limits<value_type>::max() / quantity) {
      throw PriceLevelError("trade total value overflow");
    }
    return price * quantity;
  }

  friend bool operator==(const Trade&, const Trade&) = default;

private:
  static TimestampMs current_timestamp_ms() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<TimestampMs>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
      ).count()
    );
  }

  TradeId trade_id_{};
  OrderId taker_order_id_{};
  OrderId maker_order_id_{};
  Price price_{};
  Quantity quantity_{};
  Side taker_side_{};
  TimestampMs timestamp_{};
};

class TradeList {
public:
  TradeList() = default;

  explicit TradeList(std::vector<Trade> trades)
    : trades_(std::move(trades)) {
  }

  static TradeList with_capacity(std::size_t capacity) {
    TradeList result;
    result.trades_.reserve(capacity);
    return result;
  }

  void add(Trade trade) {
    trades_.push_back(std::move(trade));
  }

  const std::vector<Trade>& as_vector() const {
    return trades_;
  }

  std::vector<Trade> into_vector() && {
    return std::move(trades_);
  }

  bool empty() const {
    return trades_.empty();
  }

  std::size_t size() const {
    return trades_.size();
  }

private:
  std::vector<Trade> trades_;
};

} // namespace lemon
