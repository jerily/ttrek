#ifndef SOLVABLE_H
#define SOLVABLE_H

#include <memory>
#include <string>
#include <iostream>
#include <cassert>
#include "internal/StringId.h"
#include "internal/NameId.h"
#include "internal/VersionSetId.h"
#include "internal/SolvableId.h"
#include "Pool.h"

// The Solvable class template represents a single candidate of a package
template<typename V>
class Solvable {
private:
    V inner;   // The record associated with this solvable
    NameId name; // The interned name in the Pool

public:
// Constructor
    Solvable(const V &record, NameId nameId) : inner(record), name(nameId) {}

// Accessor for the record
    const V &get_inner() const {
        return inner;
    }

// Accessor for the name ID
    NameId get_name_id() const {
        return name;
    }
};

// The inner representation of a solvable, which can be either a package or the root solvable
enum class SolvableInnerType {
    Root,
    Package
};

// The InternalSolvable class template represents a package that can be installed
template<typename V>
class InternalSolvable {
private:

    SolvableInnerType type;
    std::unique_ptr<Solvable<V>> package;

public:
// Constructor for root solvable
    InternalSolvable() : type(SolvableInnerType::Root) {}

// Constructor for package solvable
    InternalSolvable(NameId name, const V &record)
            : type(SolvableInnerType::Package), package(std::make_unique<Solvable<V>>(record, name)) {}

    static InternalSolvable new_root() {
        return InternalSolvable();
    }

// Check if the solvable is root
    bool is_root() const {
        return type == SolvableInnerType::Root;
    }

// Get the solvable if it's not root
    const Solvable<V> *get_solvable() const {
        assert(type == SolvableInnerType::Package);
        return package.get();
    }

    SolvableInnerType get_type() const {
        return type;
    }
};


#endif // SOLVABLE_H