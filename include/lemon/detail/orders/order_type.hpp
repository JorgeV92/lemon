#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include <lemon/utils/value.hpp>

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

  std::string_view to_string() const {
    switch (type) {
      case TimeInForce::GoodTillCanceled:
        return "GTC";
      case TimeInForce::ImmediateOrCancel:
        return "IOC";
      case TimeInForce::FillOrKill:
        return "FOK";
      case TimeInForce::GoodTillDate:
        return "GTD";
      case TimeInForce::Day:
        return "DAY";
    }

    return "UNKNOWN";
  }

  bool is_immediate() const {
    return type == TimeInForce::ImmediateOrCancel ||
           type == TimeInForce::FillOrKill;
  }

  bool has_expiry() const {
    return type == TimeInForce::GoodTillDate ||
           type == TimeInForce::Day;
  }

  bool is_expired(
    std::uint64_t current_timestamp,
    std::optional<std::uint64_t> market_close_timestamp
  ) const {
    switch (type) {
      case TimeInForce::GoodTillDate:
        return expiry_timestamp.has_value() && current_timestamp >= *expiry_timestamp;
      case TimeInForce::Day:
        return market_close_timestamp.has_value() && current_timestamp >= *market_close_timestamp;
      case TimeInForce::GoodTillCanceled:
      case TimeInForce::ImmediateOrCancel: 
      case TimeInForce::FillOrKill:
        return false;
    }
    return false;
  }


};

enum class PegReferenceType : std::uint8_t {
  BestBid,
  BestAsk,
  MidPrice,
  LastTrade
};

class OrderType {
public:
  OrderType() = default;

  OrderType(OrderId id,
            Side side,
            Price price,
            Quantity quantity,
            Quantity visible_quantity,
            Quantity hidden_quantity,
            TimestampMs timestamp,
            TimeInForcePolicy time_in_force = {},
            std::optional<TimestampMs> expires_at = std::nullopt)
    : id_(id),
      side_(side),
      price_(price),
      quantity_(quantity),
      visible_quantity_(visible_quantity),
      hidden_quantity_(hidden_quantity),
      timestamp_(timestamp),
      time_in_force_(time_in_force),
      expires_at_(expires_at) {
  }

  OrderId get_id() const {
    return id_;
  }

  Quantity get_visible_quantity() const {
    return visible_quantity_;
  }

  Price get_price() const {
    return price_;
  }

  Quantity get_hidden_quantity() const {
    return hidden_quantity_;
  }

  Quantity get_quantity() const {
    return quantity_;
  }

  Side get_side() const {
    return side_;
  }

  TimeInForcePolicy get_time_in_force() const {
    return time_in_force_;
  }

  TimestampMs get_timestamp() const {
    return timestamp_;
  }

  bool is_buy() const {
    return side_ == Side::Buy;
  }

  bool is_sell() const {
    return side_ == Side::Sell;
  }

  bool has_hidden_quantity() const {
    return hidden_quantity_.value() > 0;
  }

  bool is_fully_visible() const {
    return !has_hidden_quantity() && visible_quantity_ == quantity_;
  }

  Quantity total_quantity() const {
    return Quantity{visible_quantity_.value() + hidden_quantity_.value()};
  }

  std::optional<TimestampMs> get_expires_at() const {
    return expires_at_;
  }

private:
  OrderId id_{};
  Side side_{};
  Price price_{};
  Quantity quantity_{};
  Quantity visible_quantity_{};
  Quantity hidden_quantity_{};
  TimestampMs timestamp_{};
  TimeInForcePolicy time_in_force_{};
  std::optional<TimestampMs> expires_at_{};
};

struct LimitOrder {
  OrderType order;
};

struct IcebergOrder {
  OrderType order;
  Quantity peak_quantity{};
};

struct PostOnlyOrder {
  OrderType order;
};

struct FillOrKillOrder {
  OrderType order;
};

struct ReserveOrder {
  OrderType order;
  Quantity reserve_quantity{};
  Quantity replenish_amount{};
};

} // namespace lemon
