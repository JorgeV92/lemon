#pragma once 

#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>


#include <lemon/utils/value.hpp>

namespace lemon {

class PriceLevel;

class OrderBookEntry {
public:
  OrderBookEntry(std::shared_ptr<PriceLevel> level, std::size_t index) 
    : level_(std::move(level)), index_(index) {
    
    if (!level_)
        throw std::invalid_argument("OrderBookEntry requires a non-null PriceLevel");
  }

  Price price() const;

  Quantity visible_quantity() const;

  Quantity total_quantity() const;

  std::size_t order_count() const;

  std::size_t index() const {
    return index_;
  }

  const std::shared_ptr<PriceLevel>& level() const {
    return level_;
  }

  friend bool operator==(const OrderBookEntry& a, const OrderBookEntry& b) {
    return a.price() == b.price();
  }

  friend auto operator<=>(const OrderBookEntry& a, const OrderBookEntry& b) {
    return a.price() <=> b.price();
  }

private:
    std::shared_ptr<PriceLevel> level_;
    std::size_t index_;
};
  
} // namespace lemon
