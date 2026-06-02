
#include <lemon/detail/orderBook.hpp>
#include <lemon/detail/pricelevel/entry.hpp>
#include <lemon/detail/pricelevel/level.hpp>
#include <lemon/detail/pricelevel/order_queue.hpp>

namespace lemon {

void OrderBook::set_market_close_timestamp(std::uint64_t timestamp) {
    market_close_timestamp_.store(timestamp, std::memory_order_seq_cst);
    has_market_close_.store(true, std::memory_order_seq_cst);
}

void OrderBook::clear_market_close_timestamp() {
    has_market_close_.store(false, std::memory_order_seq_cst);
}

std::optional<uint64_t> OrderBook::best_bid() {
    std::optional<std::uint64_t> best_price{std::nullopt};
    for (auto& [price, price_level] : bids_) {
      if (!best_price || price > *best_price) {
        best_price = price;
      }
    }
  return best_price;
}

} // namespace lemon
