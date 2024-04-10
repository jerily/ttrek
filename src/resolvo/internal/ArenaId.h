#include <cstddef>
#include <cstdint>
#include <cassert>

class ArenaId {
public:
    virtual ~ArenaId() = default;

    virtual std::size_t to_usize() const = 0;

    static std::uint32_t from_usize(std::size_t x) {
        assert(x <= static_cast<std::size_t>(UINT32_MAX));
        return static_cast<std::uint32_t>(x);
    }

};
