#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <lemon/detail/errors.hpp>
#include <lemon/detail/execution/match_result.hpp>
#include <lemon/detail/orders/order_type.hpp>
#include <lemon/detail/orders/order_update.hpp>
#include <lemon/detail/pricelevel/order_queue.hpp>
#include <lemon/detail/pricelevel/snapshot.hpp>
#include <lemon/detail/pricelevel/statistics.hpp>
#include <lemon/utils/value.hpp>

namespace lemon {

using OrderId = std::uint64_t;

class PriceLevel {
public:
  explicit PriceLevel(Price price);

  PriceLevel(Price price, 
             Quantity visible_quantity,
             Quantity hidden_quantity,
             std::size_t order_count,
             std::vector<std::shared_ptr<OrderType>> orders);

  static PriceLevel from_snapshot(PriceLevelSnapshot snapshot);

  Price price() const;

  Quantity visible_quantity() const;

  Quantity hidden_quantity() const;

  Quantity total_quantity() const;

  std::size_t order_count() const;

  std::shared_ptr<PriceLevelStatistics> stats() const;

  std::shared_ptr<OrderType> add_order(OrderType order);

  std::vector<std::shared_ptr<OrderType>> iter_orders() const;

  std::vector<std::shared_ptr<OrderType>> snapshot_orders() const;

  MatchResult match_order(const OrderType& order);

  void update_order(const OrderUpdate& update);

  PriceLevelSnapshot snapshot() const;

private:
  Price price_;
  std::atomic<std::uint64_t> visible_quantity_{0};
  std::atomic<std::uint64_t> hidden_quantity_{0};
  std::atomic_size_t order_count_{0};
  OrderQueue orders_;
  std::shared_ptr<PriceLevelStatistics> stats_;
};

} // namespace lemon
