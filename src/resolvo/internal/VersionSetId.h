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
