

#include <memory>
#include <atomic>
#include <utility>

#include <lemon/detail/pricelevel/level.hpp>

namespace lemon {

PriceLevel::PriceLevel(Price price)
  : price_(price),
    stats_(std::make_shared<PriceLevelStatistics>()) {
}

PriceLevel::PriceLevel(Price price,
                       Quantity visible_quantity,
                       Quantity hidden_quantity,
                       std::size_t order_count,
                       std::vector<std::shared_ptr<OrderType>> orders)
  : price_(price),
    visible_quantity_(visible_quantity.value()),
    hidden_quantity_(hidden_quantity.value()),
    order_count_(order_count),
    orders_(orders),
    stats_(std::make_shared<PriceLevelStatistics>()) {
}

PriceLevel PriceLevel::from_snapshot(PriceLevelSnapshot snapshot) {
  snapshot.refresh_aggregates();

  return PriceLevel{
    snapshot.price(),
    snapshot.visible_quantity(),
    snapshot.hidden_quantity(),
    snapshot.order_count(),
    snapshot.into_orders()
  };
}

Price PriceLevel::price() const {
  return price_;
}

Quantity PriceLevel::visible_quantity() const {
  return Quantity{visible_quantity_.load(std::memory_order_seq_cst)};
}

Quantity PriceLevel::hidden_quantity() const {
  return Quantity{hidden_quantity_.load(std::memory_order_seq_cst)};
}

Quantity PriceLevel::total_quantity() const {
  return Quantity{
    visible_quantity_.load(std::memory_order_seq_cst) +
    hidden_quantity_.load(std::memory_order_seq_cst)
  };
}

std::size_t PriceLevel::order_count() const {
  return order_count_.load(std::memory_order_seq_cst);
}

std::shared_ptr<PriceLevelStatistics> PriceLevel::stats() const {
  return stats_;
}

} // namespace lemon
