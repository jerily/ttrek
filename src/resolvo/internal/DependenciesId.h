#ifndef DEPENDENCIES_ID_H
#define DEPENDENCIES_ID_H

#include "ArenaId.h"

class DependenciesId : public ArenaId {
private:
    std::uint32_t value;

public:
    explicit DependenciesId(std::uint32_t value) : value(value) {}

    std::size_t to_usize() const override {
        return static_cast<std::size_t>(value);
    }

};

#endif // DEPENDENCIES_ID_H