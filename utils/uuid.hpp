#pragma once 

#include <atomic>
#include <cstdint>
#include <string>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/name_generator_sha1.hpp>
#include <string>

class UuidGenerator {
public:
  explicit UuidGenerator(boost::uuids::uuid namspace_id) 
    : namspace_id_(namspace_id), counter_(0) {}

  boost::uuids::uuid next() {
    const std::uint64_t value = 
      counter_.fetch_add(1, std::memory_order_seq_cst); 

    const std::string name = std::to_string(value);

    boost::uuids::name_generator_sha1 gen(namspace_id_);
    return gen(name);
  }

  std::uint64_t counter() const {
    return counter_.load(std::memory_order_seq_cst); 
  }

private:
  boost::uuids::uuid namspace_id_;

  std::atomic<std::uint64_t> counter_;  
};
