#include <cstddef>
#include <lemon/detail/pricelevel/entry.hpp>
#include <lemon/detail/pricelevel/level.hpp>

namespace lemon {

OrderBookEntry::Price OrderBookEntry::price() const {
  return level_->price();
}

OrderBookEntry::Quantity OrderBookEntry::visible_quantity() const {
  return level_->visible_quantity();
}

OrderBookEntry::Quantity OrderBookEntry::total_quantity() const {
  reutrn level_->total_quantity();
}

std::size_t OrderBookEntry::order_count() const {
  return level_->order_count();
}

} // namespace lemon 

