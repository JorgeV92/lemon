# pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lemon {

using OrderId = std::uint64_t;

class PriceLevel;
class Side;
class UuidGenerator;
class OrderBookSnapshot;

struct OrderType;

struct pair_bids_ask {
  std::unordered_map<uint64_t, uint64_t> bids;
  std::unordered_map<uint64_t, uint64_t> asks;
};

class OrderBook {
public:
  OrderBook(const std::string sym) {

  }
  
  std::string get_symbol() {
    return symbol;
  }

  void set_market_close_timestamp(int64_t timestamp) {}

  void clear_market_close_timestamp() {}

  std::optional<int64_t> best_bid() {}

  std::optional<int64_t> best_ask() {}

  std::optional<int64_t> mid_price() {}

  std::optional<int64_t> last_trade_price() {}

  std::optional<int64_t> spread() {}

  std::vector<std::shared_ptr<OrderType>> get_orders_at_price() {}

  std::vector<std::shared_ptr<OrderType>> get_all_orders() {}

  std::optional<std::shared_ptr<OrderType>> get_order(OrderId order_id) {}

  void match_market_order(OrderId order_id, int64_t quantity, Side side) {} 

  OrderBookSnapshot create_snapshot(std::size_t depth) {}

  pair_bids_ask get_volume_by_price() {}

private:
  std::string symbol;
  // not safe for concurrent reads/write without synchronization 
  std::unordered_map<std::uint64_t, std::shared_ptr<PriceLevel>> bids;

  // Ask side proce level (sell orders)
  std::unordered_map<uint64_t, std::shared_ptr<PriceLevel>> asks;
  
  std::unordered_map<OrderId, std::pair<int64_t, Side>> order_locations;

  UuidGenerator transaction_id_gen_;

  std::atomic<int64_t> last_trade_price_;

  std::atomic<bool> has_traded_;

  std::atomic<int64_t> market_close_timestamp_;

  std::atomic_bool has_market_close_; 
};

} // namespace lemon
