#ifndef NAMEID_H
#define NAMEID_H

#include <cstddef>
#include <cstdint>
#include <cassert>
#include "ArenaId.h"

// The id associated to a package name
class NameId : public ArenaId {
private:
    std::uint32_t value;

public:
    explicit NameId(std::uint32_t value) : value(value) {}

    std::size_t to_usize() const override {
        return static_cast<std::size_t>(value);
    }
};

#endif // NAMEID_H