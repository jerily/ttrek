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

// The InternalSolvable class template represents a package that can be installed
template<typename V>
class InternalSolvable {
private:
// The inner representation of a solvable, which can be either a package or the root solvable
    enum class SolvableInnerType {
        Root,
        Package
    };

    SolvableInnerType type;
    std::unique_ptr<Solvable<V>> package;

public:
// Constructor for root solvable
    InternalSolvable() : type(SolvableInnerType::Root) {}

// Constructor for package solvable
    InternalSolvable(NameId name, const V &record)
            : type(SolvableInnerType::Package), package(std::make_unique<Solvable<V>>(record, name)) {}

// Check if the solvable is root
    bool is_root() const {
        return type == SolvableInnerType::Root;
    }

// Get the solvable if it's not root
    const Solvable<V> *get_solvable() const {
        assert(type == SolvableInnerType::Package);
        return package.get();
    }
};

// The DisplaySolvable class template is used to visualize a solvable
template<typename VS, typename N>
class DisplaySolvable {
private:
    const Pool<VS, N> &pool;
    const InternalSolvable<typename VS::V> &solvable;

public:
// Constructor
    DisplaySolvable(const Pool<VS, N> &poolRef, const InternalSolvable<typename VS::V> &solvableRef)
            : pool(poolRef), solvable(solvableRef) {}

// Display function
    void display() const {
        if (solvable.is_root()) {
            std::cout << "<root>";
        } else {
            const auto *solv = solvable.get_solvable();
// Assuming PackageName has an operator<< for display
            std::cout << pool.resolve_package_name(solv->get_name_id()) << "=" << solv->get_inner();
        }
    }
};

#endif // SOLVABLE_H