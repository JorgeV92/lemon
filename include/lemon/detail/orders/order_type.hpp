#pragma once

#include <array>
#include <algorithm>
#include <compare>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

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
        return expiry_timestamp.has_value() &&
               current_timestamp >= *expiry_timestamp;
      case TimeInForce::Day:
        return market_close_timestamp.has_value() &&
               current_timestamp >= *market_close_timestamp;
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

inline std::string_view to_string(PegReferenceType reference) {
  switch (reference) {
    case PegReferenceType::BestBid:
      return "BestBid";
    case PegReferenceType::BestAsk:
      return "BestAsk";
    case PegReferenceType::MidPrice:
      return "MidPrice";
    case PegReferenceType::LastTrade:
      return "LastTrade";
  }
  return "Unknown";
}

// Runtime values are validated at construction, so ReserveOrder can never
// contain a zero replenishment amount.
class NonZeroQuantity {
public:
  explicit constexpr NonZeroQuantity(std::uint64_t value)
    : value_(value) {
    if (value == 0) {
      throw std::invalid_argument("NonZeroQuantity requires a value greater than zero");
    }
  }

  constexpr std::uint64_t value() const {
    return value_;
  }

  friend constexpr auto operator<=>(
    const NonZeroQuantity&,
    const NonZeroQuantity&
  ) = default;

private:
  std::uint64_t value_;
};

inline constexpr NonZeroQuantity default_reserve_replenish_amount{80};

enum class OrderKind : std::uint8_t {
  Standard,
  Iceberg,
  PostOnly,
  TrailingStop,
  Pegged,
  MarketToLimit,
  Reserve
};

inline std::string_view to_string(OrderKind kind) {
  switch (kind) {
    case OrderKind::Standard:
      return "standard";
    case OrderKind::Iceberg:
      return "iceberg";
    case OrderKind::PostOnly:
      return "post_only";
    case OrderKind::TrailingStop:
      return "trailing_stop";
    case OrderKind::Pegged:
      return "pegged";
    case OrderKind::MarketToLimit:
      return "market_to_limit";
    case OrderKind::Reserve:
      return "reserve";
  }
  return "unknown";
}

struct StandardOrder {
  Quantity quantity{};
};

struct IcebergOrder {
  Quantity visible_quantity{};
  Quantity hidden_quantity{};
};

struct PostOnlyOrder {
  Quantity quantity{};
};

struct TrailingStopOrder {
  Quantity quantity{};
  Quantity trail_amount{};
  Price last_reference_price{};
};

struct PeggedOrder {
  Quantity quantity{};
  std::int64_t reference_price_offset{};
  PegReferenceType reference_price_type{PegReferenceType::BestBid};
};

struct MarketToLimitOrder {
  Quantity quantity{};
};

struct ReserveOrder {
  Quantity visible_quantity{};
  Quantity hidden_quantity{};
  Quantity replenish_threshold{};
  std::optional<NonZeroQuantity> replenish_amount{};
  bool auto_replenish{false};
};

using LimitOrder = StandardOrder;
// Fill-or-kill is a time-in-force policy rather than a distinct order variant.
using FillOrKillOrder = StandardOrder;

using OrderVariant = std::variant<
  StandardOrder,
  IcebergOrder,
  PostOnlyOrder,
  TrailingStopOrder,
  PeggedOrder,
  MarketToLimitOrder,
  ReserveOrder
>;

class OrderType;

struct OrderMatchResult;

class OrderType {
public:
  OrderType() = default;

  // Backward-compatible constructor. Orders carrying hidden quantity (or a
  // visible quantity different from quantity) become iceberg orders; fully
  // visible orders become standard orders.
  OrderType(
    OrderId id,
    Side side,
    Price price,
    Quantity quantity,
    Quantity visible_quantity,
    Quantity hidden_quantity,
    TimestampMs timestamp,
    TimeInForcePolicy time_in_force = {},
    std::optional<TimestampMs> expires_at = std::nullopt
  )
    : id_(id),
      side_(side),
      price_(price),
      timestamp_(timestamp),
      time_in_force_(time_in_force),
      expires_at_(expires_at),
      variant_(legacy_variant(quantity, visible_quantity, hidden_quantity)) {
  }

  static OrderType standard(
    OrderId id,
    Side side,
    Price price,
    Quantity quantity,
    TimestampMs timestamp,
    TimeInForcePolicy time_in_force = {},
    std::optional<TimestampMs> expires_at = std::nullopt,
    UserHash user_id = {}
  ) {
    return OrderType{
      id, side, price, timestamp, time_in_force, expires_at, user_id,
      StandardOrder{quantity}
    };
  }

  static OrderType iceberg(
    OrderId id,
    Side side,
    Price price,
    Quantity visible_quantity,
    Quantity hidden_quantity,
    TimestampMs timestamp,
    TimeInForcePolicy time_in_force = {},
    std::optional<TimestampMs> expires_at = std::nullopt,
    UserHash user_id = {}
  ) {
    return OrderType{
      id, side, price, timestamp, time_in_force, expires_at, user_id,
      IcebergOrder{visible_quantity, hidden_quantity}
    };
  }

  static OrderType post_only(
    OrderId id,
    Side side,
    Price price,
    Quantity quantity,
    TimestampMs timestamp,
    TimeInForcePolicy time_in_force = {},
    std::optional<TimestampMs> expires_at = std::nullopt,
    UserHash user_id = {}
  ) {
    return OrderType{
      id, side, price, timestamp, time_in_force, expires_at, user_id,
      PostOnlyOrder{quantity}
    };
  }

  static OrderType trailing_stop(
    OrderId id,
    Side side,
    Price price,
    Quantity quantity,
    Quantity trail_amount,
    Price last_reference_price,
    TimestampMs timestamp,
    TimeInForcePolicy time_in_force = {},
    std::optional<TimestampMs> expires_at = std::nullopt,
    UserHash user_id = {}
  ) {
    return OrderType{
      id, side, price, timestamp, time_in_force, expires_at, user_id,
      TrailingStopOrder{quantity, trail_amount, last_reference_price}
    };
  }

  static OrderType pegged(
    OrderId id,
    Side side,
    Price price,
    Quantity quantity,
    std::int64_t reference_price_offset,
    PegReferenceType reference_price_type,
    TimestampMs timestamp,
    TimeInForcePolicy time_in_force = {},
    std::optional<TimestampMs> expires_at = std::nullopt,
    UserHash user_id = {}
  ) {
    return OrderType{
      id, side, price, timestamp, time_in_force, expires_at, user_id,
      PeggedOrder{quantity, reference_price_offset, reference_price_type}
    };
  }

  static OrderType market_to_limit(
    OrderId id,
    Side side,
    Price price,
    Quantity quantity,
    TimestampMs timestamp,
    TimeInForcePolicy time_in_force = {},
    std::optional<TimestampMs> expires_at = std::nullopt,
    UserHash user_id = {}
  ) {
    return OrderType{
      id, side, price, timestamp, time_in_force, expires_at, user_id,
      MarketToLimitOrder{quantity}
    };
  }

  static OrderType reserve(
    OrderId id,
    Side side,
    Price price,
    Quantity visible_quantity,
    Quantity hidden_quantity,
    Quantity replenish_threshold,
    std::optional<NonZeroQuantity> replenish_amount,
    bool auto_replenish,
    TimestampMs timestamp,
    TimeInForcePolicy time_in_force = {},
    std::optional<TimestampMs> expires_at = std::nullopt,
    UserHash user_id = {}
  ) {
    return OrderType{
      id, side, price, timestamp, time_in_force, expires_at, user_id,
      ReserveOrder{
        visible_quantity,
        hidden_quantity,
        replenish_threshold,
        replenish_amount,
        auto_replenish
      }
    };
  }

  OrderId get_id() const {
    return id_;
  }

  OrderKind kind() const {
    return std::visit(
      [](const auto& order) -> OrderKind {
        using T = std::decay_t<decltype(order)>;
        if constexpr (std::is_same_v<T, StandardOrder>) {
          return OrderKind::Standard;
        } else if constexpr (std::is_same_v<T, IcebergOrder>) {
          return OrderKind::Iceberg;
        } else if constexpr (std::is_same_v<T, PostOnlyOrder>) {
          return OrderKind::PostOnly;
        } else if constexpr (std::is_same_v<T, TrailingStopOrder>) {
          return OrderKind::TrailingStop;
        } else if constexpr (std::is_same_v<T, PeggedOrder>) {
          return OrderKind::Pegged;
        } else if constexpr (std::is_same_v<T, MarketToLimitOrder>) {
          return OrderKind::MarketToLimit;
        } else {
          return OrderKind::Reserve;
        }
      },
      variant_
    );
  }

  const OrderVariant& variant() const {
    return variant_;
  }

  template <typename T>
  const T* get_if() const {
    return std::get_if<T>(&variant_);
  }

  Quantity get_visible_quantity() const {
    return std::visit(
      [](const auto& order) -> Quantity {
        using T = std::decay_t<decltype(order)>;
        if constexpr (std::is_same_v<T, IcebergOrder> ||
                      std::is_same_v<T, ReserveOrder>) {
          return order.visible_quantity;
        } else {
          return order.quantity;
        }
      },
      variant_
    );
  }

  Quantity get_hidden_quantity() const {
    return std::visit(
      [](const auto& order) -> Quantity {
        using T = std::decay_t<decltype(order)>;
        if constexpr (std::is_same_v<T, IcebergOrder> ||
                      std::is_same_v<T, ReserveOrder>) {
          return order.hidden_quantity;
        } else {
          return Quantity::zero();
        }
      },
      variant_
    );
  }

  // For iceberg/reserve orders this is their current visible + hidden total.
  Quantity get_quantity() const {
    return total_quantity();
  }

  Price get_price() const {
    return price_;
  }

  Side get_side() const {
    return side_;
  }

  const UserHash& get_user_id() const {
    return user_id_;
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
    return get_hidden_quantity().value() > 0;
  }

  bool is_fully_visible() const {
    return !has_hidden_quantity();
  }

  bool is_immediate() const {
    return time_in_force_.is_immediate();
  }

  bool is_fill_or_kill() const {
    return time_in_force_.type == TimeInForce::FillOrKill;
  }

  bool is_post_only() const {
    return kind() == OrderKind::PostOnly;
  }

  bool is_matchable() const {
    if (get_visible_quantity().value() > 0) {
      return true;
    }

    if (const auto* iceberg_order = get_if<IcebergOrder>()) {
      return iceberg_order->hidden_quantity.value() > 0;
    }

    if (const auto* reserve_order = get_if<ReserveOrder>()) {
      return reserve_order->auto_replenish &&
             reserve_order->hidden_quantity.value() > 0;
    }

    return false;
  }

  Quantity total_quantity() const {
    return Quantity{
      get_visible_quantity().value() + get_hidden_quantity().value()
    };
  }

  std::optional<TimestampMs> get_expires_at() const {
    return expires_at_;
  }

  OrderType with_reduced_quantity(std::uint64_t new_quantity) const;

  std::pair<OrderType, std::uint64_t> refresh_iceberg(
    NonZeroQuantity refresh_amount
  ) const;

  OrderMatchResult match_against(std::uint64_t incoming_quantity) const;

private:
  OrderType(
    OrderId id,
    Side side,
    Price price,
    TimestampMs timestamp,
    TimeInForcePolicy time_in_force,
    std::optional<TimestampMs> expires_at,
    UserHash user_id,
    OrderVariant variant
  )
    : id_(id),
      side_(side),
      price_(price),
      timestamp_(timestamp),
      time_in_force_(time_in_force),
      expires_at_(expires_at),
      user_id_(user_id),
      variant_(std::move(variant)) {
  }

  static OrderVariant legacy_variant(
    Quantity quantity,
    Quantity visible_quantity,
    Quantity hidden_quantity
  ) {
    if (hidden_quantity.value() > 0 || visible_quantity != quantity) {
      return IcebergOrder{visible_quantity, hidden_quantity};
    }
    return StandardOrder{quantity};
  }

  OrderId id_{};
  Side side_{};
  Price price_{};
  TimestampMs timestamp_{};
  TimeInForcePolicy time_in_force_{};
  std::optional<TimestampMs> expires_at_{};
  UserHash user_id_{};
  OrderVariant variant_{StandardOrder{}};
};

struct OrderMatchResult {
  std::uint64_t consumed{};
  std::optional<OrderType> updated_order{};
  std::uint64_t hidden_reduced{};
  std::uint64_t remaining_quantity{};
};

inline OrderType OrderType::with_reduced_quantity(
  std::uint64_t new_quantity
) const {
  OrderType updated = *this;
  const Quantity quantity{new_quantity};

  std::visit(
    [quantity](auto& order) {
      using T = std::decay_t<decltype(order)>;
      if constexpr (std::is_same_v<T, IcebergOrder> ||
                    std::is_same_v<T, ReserveOrder>) {
        order.visible_quantity = quantity;
      } else {
        order.quantity = quantity;
      }
    },
    updated.variant_
  );

  return updated;
}

inline std::pair<OrderType, std::uint64_t> OrderType::refresh_iceberg(
  NonZeroQuantity refresh_amount
) const {
  OrderType refreshed = *this;
  std::uint64_t used_hidden = 0;

  std::visit(
    [&](auto& order) {
      using T = std::decay_t<decltype(order)>;
      if constexpr (std::is_same_v<T, IcebergOrder> ||
                    std::is_same_v<T, ReserveOrder>) {
        used_hidden = std::min(
          refresh_amount.value(),
          order.hidden_quantity.value()
        );
        order.visible_quantity = Quantity{used_hidden};
        order.hidden_quantity = Quantity{
          order.hidden_quantity.value() - used_hidden
        };
      }
    },
    refreshed.variant_
  );

  return {std::move(refreshed), used_hidden};
}

inline OrderMatchResult OrderType::match_against(
  std::uint64_t incoming_quantity
) const {
  if (const auto* iceberg_order = get_if<IcebergOrder>()) {
    const std::uint64_t visible = iceberg_order->visible_quantity.value();
    const std::uint64_t hidden = iceberg_order->hidden_quantity.value();

    if (visible <= incoming_quantity) {
      const std::uint64_t remaining = incoming_quantity - visible;
      if (hidden == 0) {
        return {visible, std::nullopt, 0, remaining};
      }

      const std::uint64_t tranche = visible == 0 ? hidden : visible;
      const std::uint64_t refreshed_quantity = std::min(hidden, tranche);
      OrderType updated = *this;
      auto& updated_iceberg = std::get<IcebergOrder>(updated.variant_);
      updated_iceberg.visible_quantity = Quantity{refreshed_quantity};
      updated_iceberg.hidden_quantity = Quantity{hidden - refreshed_quantity};
      return {
        visible,
        std::move(updated),
        refreshed_quantity,
        remaining
      };
    }

    OrderType updated = *this;
    std::get<IcebergOrder>(updated.variant_).visible_quantity = Quantity{
      visible - incoming_quantity
    };
    return {incoming_quantity, std::move(updated), 0, 0};
  }

  if (const auto* reserve_order = get_if<ReserveOrder>()) {
    const std::uint64_t visible = reserve_order->visible_quantity.value();
    const std::uint64_t hidden = reserve_order->hidden_quantity.value();
    const std::uint64_t safe_threshold =
      reserve_order->auto_replenish &&
      reserve_order->replenish_threshold.value() == 0
        ? 1
        : reserve_order->replenish_threshold.value();
    const std::uint64_t replenish_quantity = std::min(
      reserve_order->replenish_amount
        .value_or(default_reserve_replenish_amount)
        .value(),
      hidden
    );

    if (visible <= incoming_quantity) {
      const std::uint64_t remaining = incoming_quantity - visible;
      if (hidden > 0 && reserve_order->auto_replenish) {
        OrderType updated = *this;
        auto& updated_reserve = std::get<ReserveOrder>(updated.variant_);
        updated_reserve.visible_quantity = Quantity{replenish_quantity};
        updated_reserve.hidden_quantity = Quantity{hidden - replenish_quantity};
        return {
          visible,
          std::move(updated),
          replenish_quantity,
          remaining
        };
      }
      return {visible, std::nullopt, 0, remaining};
    }

    const std::uint64_t new_visible = visible - incoming_quantity;
    OrderType updated = *this;
    auto& updated_reserve = std::get<ReserveOrder>(updated.variant_);

    if (new_visible < safe_threshold &&
        hidden > 0 &&
        reserve_order->auto_replenish) {
      updated_reserve.visible_quantity = Quantity{
        new_visible + replenish_quantity
      };
      updated_reserve.hidden_quantity = Quantity{hidden - replenish_quantity};
      return {
        incoming_quantity,
        std::move(updated),
        replenish_quantity,
        0
      };
    }

    updated_reserve.visible_quantity = Quantity{new_visible};
    return {incoming_quantity, std::move(updated), 0, 0};
  }

  const std::uint64_t visible = get_visible_quantity().value();
  if (visible <= incoming_quantity) {
    return {
      visible,
      std::nullopt,
      0,
      incoming_quantity - visible
    };
  }

  return {
    incoming_quantity,
    with_reduced_quantity(visible - incoming_quantity),
    0,
    0
  };
}

} // namespace lemon
