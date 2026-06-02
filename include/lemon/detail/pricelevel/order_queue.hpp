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

#include <lemon/detail/orders/order_type.hpp>

namespace lemon {

class OrderQueue {
public:
  OrderQueue() = default;

  void push(std::shared_ptr<OrderType> order) {
    if (!order) return;

    std::lock_guard<std::mutex> lock(mutex_);
    const OrderId order_id = order->get_id();

    orders_[order_id] = std::move(order);
    order_ids.push_back(order_id);
  }

  std::optional<std::shared_ptr<OrderType>> pop() {
    std::lock_guard<std::mutex> lock(mutex_);

    while (!order_ids.empty()) {
      OrderId order_id = order_ids.front();
      order_ids.pop_front();

      auto it = orders_.find(order_id);

      if (it == orders_.end())
          continue;

      std::shared_ptr<OrderType> order = it->second;
      return order;
    }
    return std::nullopt;
  }

  std::optional<std::shared_ptr<OrderType>> find(OrderId order_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = orders_.find(order_id);
    if (it == orders_.end()) 
      return std::nullopt;

    return it->second;
  }

  std::vector<std::shared_ptr<OrderType>> snapshot_vec() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::shared_ptr<OrderType>> result;
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

  std::vector<std::shared_ptr<OrderType>> to_vec() const {
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

  std::unordered_map<OrderId, std::shared_ptr<OrderType>> orders_;
  std::deque<OrderId> order_ids;
};

} // namespace lemon
