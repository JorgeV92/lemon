#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include <lemon/detail/orders/order_type.hpp>
#include <lemon/utils/value.hpp>

namespace lemon {

using TradeId = std::uint64_t;

struct Trade {
  TradeId trade_id_{};
  OrderId taker_order_id_{};
  OrderId maker_order_id_{};
  Price price_{};
  Quantity quantity_{};
  Side taker_side_{};
  TimestampMs timestamp_{};

  Trade() = default;

  Trade(TradeId trade_id,
        OrderId taker_order_id,
        OrderId maker_order_id,
        Price price,
        Quantity quantity,
        Side taker_side)
    : trade_id_(trade_id),
      taker_order_id_(taker_order_id),
      maker_order_id_(maker_order_id),
      price_(price),
      quantity_(quantity),
      taker_side_(taker_side),
      timestamp_(current_timestamp_ms()) {
  }

  Trade(TradeId trade_id,
        OrderId taker_order_id,
        OrderId maker_order_id,
        Price price,
        Quantity quantity,
        Side taker_side,
        TimestampMs timestamp)
    : trade_id_(trade_id),
      taker_order_id_(taker_order_id),
      maker_order_id_(maker_order_id),
      price_(price),
      quantity_(quantity),
      taker_side_(taker_side),
      timestamp_(timestamp) {
  }

  Side maker_side() const {
    return taker_side_ == Side::Buy ? Side::Sell : Side::Buy;
  }

  value_type total_value() const {
    return price_.value() * quantity_.value();
  }

private:
  static TimestampMs current_timestamp_ms() {
    const auto now = std::chrono::system_clock::now();
    const auto duration = now.time_since_epoch();
    return static_cast<TimestampMs>(
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()
    );
  }
};

struct TradeList {
  std::vector<Trade> trades;
};

} // namespace lemon
