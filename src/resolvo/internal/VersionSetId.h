#ifndef VERSION_SET_ID_H
#define VERSION_SET_ID_H

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <functional>
#include "ArenaId.h"

// The id associated with a VersionSet.
class VersionSetId : public ArenaId {
private:
    std::uint32_t value;

public:

    explicit VersionSetId(std::uint32_t value) : value(value) {}

    std::size_t to_usize() const override {
        return static_cast<std::size_t>(value);
    }

    static VersionSetId from_usize(std::size_t x) {
        return VersionSetId(static_cast<std::uint32_t>(x));
    }

    size_t operator()(const VersionSetId& vsid) const {
        return std::hash<uint32_t>{}(vsid.value);
    }

    bool operator==(const VersionSetId& vsid) const {
        return value == vsid.value;
    }

    bool operator!=(const VersionSetId& vsid) const {
        return value != vsid.value;
    }

    bool operator<(const VersionSetId& vsid) const {
        return value < vsid.value;
    }

    bool operator>(const VersionSetId& vsid) const {
        return value > vsid.value;
    }

    bool operator<=(const VersionSetId& vsid) const {
        return value <= vsid.value;
    }

    bool operator>=(const VersionSetId& vsid) const {
        return value >= vsid.value;
    }
};

#endif // VERSION_SET_ID_H