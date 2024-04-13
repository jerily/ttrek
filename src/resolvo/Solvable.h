#ifndef SOLVABLE_H
#define SOLVABLE_H

#include <memory>
#include <string>
#include <iostream>
#include <cassert>
#include <utility>
#include <any>
#include "internal/StringId.h"
#include "internal/NameId.h"
#include "internal/VersionSetId.h"

// The Solvable class template represents a single candidate of a package
template<typename V>
class Solvable {
private:
    V inner;   // The record associated with this solvable
    NameId name; // The interned name in the Pool

public:
// Constructor
    Solvable(const V &record, NameId nameId) : inner(record), name(std::move(nameId)) {}

// Accessor for the record
    V get_inner() const {
        return inner;
    }

// Accessor for the name ID
    NameId get_name_id() const {
        return name;
    }
};

// The inner representation of a solvable, which can be either a package or the root solvable
namespace SolvableInner {
    struct Root {
    };

    template<typename V>
    struct Package {
        Solvable<V> solvable;
    };
}

template <typename V>
using SolvableInnerVariant = std::variant<SolvableInner::Root, SolvableInner::Package<V>>;

// The InternalSolvable class template represents a package that can be installed
template<typename V>
class InternalSolvable {
private:
    SolvableInnerVariant<V> inner;

public:

    InternalSolvable(SolvableInnerVariant<V> inner) : inner(std::move(inner)) {}

    static InternalSolvable new_root() {
        return InternalSolvable(SolvableInner::Root{});
    }

    // new_solvable
    static InternalSolvable new_solvable(const NameId &name_id, V record) {
        return InternalSolvable(SolvableInner::Package<V>{Solvable<V>(record, name_id)});
    }

// Check if the solvable is root
    bool is_root() const {
        return std::holds_alternative<SolvableInner::Root>(inner);
    }

// Get the solvable if it's not root
    std::optional<Solvable<V>> get_solvable() const {
        return std::visit([](auto &&arg) -> std::optional<Solvable<V>> {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, SolvableInner::Package<V>>) {
                auto package = std::any_cast<SolvableInner::Package<V>>(arg);
                return std::make_optional(package.solvable);
            } else {
                return std::nullopt;
            }
        }, inner);
    }

    // Get the solvable if it's not root
    Solvable<V> get_solvable_unchecked() const {
        return std::get<SolvableInner::Package<V>>(inner).solvable;
    }

    SolvableInnerVariant<V> get_inner() const {
        return inner;
    }
};


#endif // SOLVABLE_H