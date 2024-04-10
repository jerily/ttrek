#ifndef LEARNTCLAUSEID_H
#define LEARNTCLAUSEID_H

#include "ArenaId.h"

class LearntClauseId : public ArenaId {
private:
    std::uint32_t value;

public:

    explicit LearntClauseId(std::uint32_t value) : value(value) {}

    std::size_t to_usize() const override {
        return static_cast<std::size_t>(value);
    }
};

#endif // LEARNTCLAUSEID_H