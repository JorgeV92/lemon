#pragma  once 

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lemon {

using OrderId = std::uint64_t;

struct Order {
  OrderId id; 
  std::uint64_t timestamp;
  std::uint64_t quantity;
  std::uint64_t price; 

  OrderId get_id() const  { return id; }

  std::uint64_t get_timestamp() const { return timestamp; }
};

class OrderQueue {
public:
  OrderQueue() = default;

  void push(std::shared_ptr<Order> order) {
    if (!order) return;

    std::lock_guard<std::mutex> lock(mutex_);
    const OrderId order_id = order->get_id();

    orders_[order_id] = std::move(order);
    order_ids.push_back(order_id);
  }

  std::optional<std::shared_ptr<Order>> pop() {
    std::lock_guard<std::mutex> lock(mutex_);

    while (!order_ids.empty()) {
      OrderId order_id = order_ids.front();
      order_ids.pop_front();

      auto it = orders_.find(order_id);

      if (it == orders_.end())
          continue;

      std::shared_ptr<Order> order = it->second;
      return order;
    }
    return std::nullopt;
  }

  std::optional<std::shared_ptr<Order>> find(OrderId order_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = orders_.find(order_id);
    if (it == orders_.end()) 
      return std::nullopt;

    std::shared_ptr<Order> removed_order = it->second;
    orders_.erase(it);

    return removed_order;
  }

  std::vector<std::shared_ptr<Order>> snapshot_vec() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::shared_ptr<Order>> result;
    result.reserve(orders_.size());

    for (const auto& [id, order] : orders_) {
      result.push_back(order);
    }

    std::sort(result.begin(), result.end(), 
              [](const auto& a, const auto& b) {
                return a->get_timestamp() < b->get_timestamp();
              }
    );

    return result;
  } 

  std::vector<std::shared_ptr<Order>> to_vec() const {
    return snapshot_vec();
  }

  bool empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return orders_.empty();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return orders_.size();
  }

private:
  mutable std::mutex mutex_;

  std::unordered_map<OrderId, std::shared_ptr<Order>> orders_;
  std::deque<OrderId> order_ids;
};

} // namespace lemon
