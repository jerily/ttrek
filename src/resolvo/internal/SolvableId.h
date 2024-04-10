#ifndef SOLVABLEID_H
#define SOLVABLEID_H

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <iostream>
#include <limits>
#include "ArenaId.h"
#include "../Pool.h"

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

    std::size_t to_usize() const override {
        return static_cast<std::size_t>(value_);
    }

    // hash
    std::size_t hash() const {
        return std::hash<uint32_t>{}(value_);
    }

private:
    uint32_t value_;
};

#endif // SOLVABLEID_H