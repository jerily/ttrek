#ifndef CANDIDATESID_H
#define CANDIDATESID_H

#include "ArenaId.h"

class CandidatesId : public ArenaId {
private:
    std::uint32_t value;

public:
    explicit CandidatesId(std::uint32_t value) : value(value) {}

    std::size_t to_usize() const override {
        return static_cast<std::size_t>(value);
    }

    static CandidatesId from_usize(std::size_t x) {
        return CandidatesId(static_cast<std::uint32_t>(x));
    }

};

#endif // CANDIDATESID_H