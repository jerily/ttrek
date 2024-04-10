#ifndef CLAUSEID_H
#define CLAUSEID_H

#include <cstddef>
#include <cstdint>
#include <cassert>
#include "ArenaId.h"

// The id associated to a solvable
class ClauseId : public ArenaId {
private:
    std::uint32_t value;

public:

    explicit ClauseId(std::uint32_t value) : value(value) {}

// There is a guarantee that ClauseId(0) will always be "Clause::InstallRoot". This assumption
// is verified by the solver.
    static ClauseId install_root() {
        return ClauseId(0);
    }

    bool is_root() const {
        return value == 0;
    }

    static ClauseId null() {
        return ClauseId(UINT32_MAX);
    }

    bool is_null() const {
        return value == UINT32_MAX;
    }

    std::size_t to_usize() const override {
        return static_cast<std::size_t>(value);
    }

    bool operator==(const ClauseId& other) const {
        return value == other.value;
    }
};

#endif // CLAUSEID_H