# Lemon 

![Lemon](img/lemon.png)

Lemon is a C++ order book library focused on modeling market state at a given moment.

It is currently ealy-stage.

## Repo Structure
 
 
  ```txt
  include/lemon/
    lemon.hpp                         Public umbrella header
 
 
  include/lemon/detail/
    orderBook.hpp                     Order book skeleton
 
 
  include/lemon/detail/orders/
    order_type.hpp                    Order side, time-in-force, and order data
    order_update.hpp                  Partial order update request data
 
 
  include/lemon/detail/pricelevel/
    level.hpp                         PriceLevel class declaration
    order_queue.hpp                   FIFO/order lookup container
    entry.hpp                         Lightweight price level entry wrapper
    snapshot.hpp                      Price level snapshot data
    statistics.hpp                    Price level statistics
 
 
  include/lemon/detail/execution/
    trade.hpp                         Trade result data
    match_result.hpp                  Match result data
 
 
  include/lemon/utils/
    value.hpp                         Price and Quantity value wrappers
    uuid.hpp                          UUID generation helper
 
 
  ## Example Direction
 
 
  The eventual usage should look roughly like:
 
 
  #include <lemon/lemon.hpp>
 
 
  int main() {
    lemon::OrderType order;
    order.id_ = 1;
    order.side_ = lemon::Side::Buy;
    order.price_ = lemon::Price{100};
    order.quantity_ = lemon::Quantity{10};
    order.visible_quantity_ = lemon::Quantity{10};
 
 
    // Future:
    // lemon::PriceLevel level{lemon::Price{100}};
    // level.add_order(order);
  }