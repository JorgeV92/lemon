#pragma once

#include <cstdint>
#include <compare>
#include <locale>
#include <ostream>
#include <stdexcept>

namespace lemon {

using value_type = std::uint64_t;

class Price {
public:
    constexpr Price() = default;

    explicit constexpr Price(value_type value) 
        : value_(value) {}

    static constexpr Price zero() {
        return Price{0};
    }

    constexpr value_type value() const {
        return value_;
    }

    friend constexpr auto operator<=>(const Price&, const Price&) = default;

private:
    value_type value_{0};
};

inline std::ostream& operator<<(std::ostream& os, Price price) {
    return os << price.value();
}


class Quantity {
public:
    constexpr Quantity() = default;

    explicit constexpr Quantity(value_type value) 
        : value_(value) {}

    static constexpr Quantity zero() {
        return Quantity{0}; 
    }

    constexpr value_type value() const {
        return value_;
    }

    friend constexpr auto operator<=>(const Quantity&, const Quantity&) = default;

private:
    value_type value_{0};
};

inline std::ostream& operator<<(std::ostream& os, Quantity quantity) {
    return os << quantity.value(); 
}

} // namespace lemon