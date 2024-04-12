#ifndef SOLVABLE_ID_H
#define SOLVABLE_ID_H

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <iostream>
#include <limits>
#include "ArenaId.h"
#include "../Pool.h"
#include "../DisplaySolvable.h"

class SolvableId: ArenaId {
public:

    explicit SolvableId(uint32_t id) : value_(id) {}

    static SolvableId root() {
        return SolvableId(0);
    }

    bool is_root() const {
        return value_ == 0;
    }

    static SolvableId null() {
        return SolvableId(std::numeric_limits<uint32_t>::max());
    }

    bool is_null() const {
        return value_ == std::numeric_limits<uint32_t>::max();
    }

    // Example function that returns a displayable object
    template<typename VS, typename N>
    DisplaySolvable<VS, N> display(const Pool<VS, N>& pool) const {
        return pool.resolve_internal_solvable(*this).display(pool);
    }

    bool operator==(const SolvableId& other) const {
        return value_ == other.value_;
    }

    bool operator!=(const SolvableId& other) const {
        return value_ != other.value_;
    }

    bool operator==(uint32_t other) const {
        return value_ == other;
    }

    bool operator!=(uint32_t other) const {
        return value_ != other;
    }

    bool operator!() const {
        return !value_;
    }

    std::size_t to_usize() const override {
        return static_cast<std::size_t>(value_);
    }

    static SolvableId from_usize(std::size_t x) {
        return SolvableId(static_cast<std::uint32_t>(x));
    }

    struct Hash {
        std::size_t operator()(const SolvableId& id) const {
            return std::hash<uint32_t>{}(id.value_);
        }
    };

private:
    uint32_t value_;
};

#endif // SOLVABLE_ID_H