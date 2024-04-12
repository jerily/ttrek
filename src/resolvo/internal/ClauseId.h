#ifndef CLAUSEID_H
#define CLAUSEID_H

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <memory>
#include "ArenaId.h"

// The id associated to a solvable
class ClauseId : public ArenaId {
public:

    explicit ClauseId(std::uint32_t value) : value_(value) {}

// There is a guarantee that ClauseId(0) will always be "Clause::InstallRoot". This assumption
// is verified by the solver.
    static ClauseId install_root() {
        return ClauseId(0);
    }

    bool is_root() const {
        return value_ == 0;
    }

    static ClauseId null() {
        return ClauseId(UINT32_MAX);
    }

    bool is_null() const {
        return value_ == UINT32_MAX;
    }

    std::size_t to_usize() const override {
        return static_cast<std::size_t>(value_);
    }

    static ClauseId from_usize(std::size_t x) {
        return ClauseId(static_cast<std::uint32_t>(x));
    }

    bool operator==(const ClauseId& other) const {
        return value_ == other.value_;
    }

    bool operator!=(const ClauseId& other) const {
        return value_ != other.value_;
    }

    struct Hash {
        std::size_t operator()(const ClauseId& id) const {
            return std::hash<uint32_t>{}(id.value_);
        }
    };

private:
    std::uint32_t value_;

};

#endif // CLAUSEID_H