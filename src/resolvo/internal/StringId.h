#include <cstddef>
#include <cstdint>
#include <cassert>
#include "ArenaId.h"

// The id associated with a generic string
class StringId : public ArenaId {
private:
    std::uint32_t value;

public:

    explicit StringId(std::uint32_t value) : value(value) {}

    std::size_t to_usize() const override {
        return static_cast<std::size_t>(value);
    }

    static StringId from_usize(std::size_t x) {
        return StringId(static_cast<std::uint32_t>(x));
    }
};
