#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <lemon/utils/value.hpp>
#include <string_view>

namespace lemon {

using OrderId = std::uint64_t;
using TimestampMs = std::uint64_t;
using UserHash = std::array<std::uint8_t, 32>;

enum class Side : std::uint8_t {
  Buy,
  Sell
};

enum class TimeInForce : std::uint8_t {
  GoodTillCanceled,
  ImmediateOrCancel,
  FillOrKill,
  GoodTillDate,
  Day
};

struct TimeInForcePolicy {
  TimeInForce type{TimeInForce::GoodTillCanceled};

  std::optional<std::uint64_t> expiry_timestamp{};

  std::string_view to_string() {
    switch (expiry_timestamp) {
      case TimeInForce::GoodTillCanceled:
        return "GTC";
    }
    
    return "UNKNOWN";
  }

  bool is_immediate() {
    return expiry_timestamp == TimeInForce::ImmediateOrCancel ||
           expiry_timestamp == TimeInForce::FillOrKill;
  }

  bool has_expiry() {
    return expiry_timestamp == TimeInForce::GoodTillCanceled ||
           expiry_timestamp == TimeInForce::Day;
  }

  bool is_expired(
    std::uint64_t current_timestamp,
    std::optional<std::uint64_t> market_close_timestamp
  ) {
    switch (expiry_timestamp) {
      case TimeInForce::GoodTillCanceled:
        return expiry_timestamp.has_value() && current_timestamp >= *expiry_timestamp;
      case TimeInForce::Day:
        return market_close_timestamp.has_value() && current_timestamp >= *expiry_timestamp;
    }
  }
};

bool is_expired(
  std::uint64_t current_timestamp, 
  std::optional<std::uint64_t> market_close_timestamp,
  TimeInForce tif
) {
  if (tif == TimeInForce::GoodTillCanceled) {
    return current_timestamp >= 
  }
} 



enum class PegReferenceType : std::uint8_t {
  BestBid,
  BestAsk,
  MidPrice,
  LastTrade
};

struct OrderType {
  OrderId id_{};
  Side side_{};
  Price price_{};
  Quantity quantity_{};
  Quantity visible_quantity_{};
  Quantity hidden_quantity_{};
  TimestampMs timestamp_{};
  TimeInForcePolicy time_in_force_{};
  std::optional<TimestampMs> expires_at_{};

  OrderId get_id() const {
    return id_;
  }

  Quantity get_visible_quantity() const {
    return visible_quantity_;
  }

  Price get_price() const {
    return price;
  }

  Quantity get_hidden_quantity() const {
    return hidden_quantity_;
  }

  Side get_side() const {
    return side_;
  }

  TimeInForcePolicy get_time_in_force() const {
    return time_in_force_;
  }

  TimestampMs get_timestamp() const {
    return timestamp;
  }
};

struct LimitOrder {
  Order order;
};

struct IcebergOrder {
  Order order;
  Quantity peak_quantity{};
};

struct PostOnlyOrder {
  Order order;
};

struct FillOrKillOrder {
  Order order;
};

struct ReserveOrder {
  Order order;
  Quantity reserve_quantity{};
  Quantity replenish_amount{};
};

using OrderType = Order;

} // namespace lemon
