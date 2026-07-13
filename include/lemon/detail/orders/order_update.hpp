#pragma once

#include <cstdint>
#include <utility>
#include <variant>

#include <lemon/detail/orders/order_type.hpp>

namespace lemon {

struct UpdatePrice {
  OrderId order_id{};
  Price new_price{};
};

struct UpdateQuantity {
  OrderId order_id{};
  Quantity new_quantity{};
};

struct UpdatePriceAndQuantity {
  OrderId order_id{};
  Price new_price{};
  Quantity new_quantity{};
};

struct CancelOrder {
  OrderId order_id{};
};

struct ReplaceOrder {
  OrderId order_id{};
  Price price{};
  Quantity quantity{};
  Side side{Side::Buy};
};

using OrderUpdateVariant = std::variant<
  UpdatePrice,
  UpdateQuantity,
  UpdatePriceAndQuantity,
  CancelOrder,
  ReplaceOrder
>;

class OrderUpdate {
public:
  explicit OrderUpdate(OrderUpdateVariant update)
    : update_(std::move(update)) {
  }

  static OrderUpdate price(OrderId order_id, Price new_price) {
    return OrderUpdate{UpdatePrice{order_id, new_price}};
  }

  static OrderUpdate quantity(OrderId order_id, Quantity new_quantity) {
    return OrderUpdate{UpdateQuantity{order_id, new_quantity}};
  }

  static OrderUpdate price_and_quantity(
    OrderId order_id,
    Price new_price,
    Quantity new_quantity
  ) {
    return OrderUpdate{
      UpdatePriceAndQuantity{order_id, new_price, new_quantity}
    };
  }

  static OrderUpdate cancel(OrderId order_id) {
    return OrderUpdate{CancelOrder{order_id}};
  }

  static OrderUpdate replace(
    OrderId order_id,
    Price price,
    Quantity quantity,
    Side side
  ) {
    return OrderUpdate{ReplaceOrder{order_id, price, quantity, side}};
  }

  OrderId order_id() const {
    return std::visit(
      [](const auto& update) { return update.order_id; },
      update_
    );
  }

  const OrderUpdateVariant& variant() const {
    return update_;
  }

private:
  OrderUpdateVariant update_;
};

} // namespace lemon
