#include <sstream>
#include <cassert>
#include <iostream>
#include "PackageDatabase.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

void test_default_sat() {
// Construct a database with packages a, b, and c.
PackageDatabase db;

    auto a_1 = db.alloc_candidate("a", "1.0.0", {{db.alloc_requirement_from_str("b", ">=1.0.0,<4.0.0")}, {}});
    auto a_2 = db.alloc_candidate("a", "2.0.0", {{db.alloc_requirement_from_str("b", ">=1.0.0,<4.0.0")}, {}});
    auto a_3 = db.alloc_candidate("a", "3.0.0", {{db.alloc_requirement_from_str("b", ">=4.0.0,<7.0.0")}, {}});

auto b_1 = db.alloc_candidate("b", "1.0.0", {});
auto b_2 = db.alloc_candidate("b", "2.0.0", {});
auto b_3 = db.alloc_candidate("b", "3.0.0", {});

auto c_1 = db.alloc_candidate("c", "1.0.0", {});

// Construct a problem to be solved by the solver
    resolvo::Vector<resolvo::VersionSetId> requirements = {db.alloc_requirement_from_str("a", ">=1.0.0,<3.0.0")};
    resolvo::Vector<resolvo::VersionSetId> constraints = {db.alloc_requirement_from_str("b", ">=1.0.0,<3.0.0"),
                                                          db.alloc_requirement_from_str("b", ">=1.0.0,<3.0.0")};

// Solve the problem
resolvo::Vector<resolvo::SolvableId> result;
auto message = resolvo::solve(db, requirements, constraints, result);

std::cout << "message: " << message << std::endl;

// Check the result
assert(result.size() == 2);
assert(result[0] == a_2);
assert(result[1] == b_2);
}

void test_default_unsat() {
// Construct a database with packages a, b, and c.
    PackageDatabase db;

    auto a_1 = db.alloc_candidate("a", "1.0.0", {{db.alloc_requirement_from_str("b", ">=1.0.0,<4.0.0")}, {}});
    auto a_2 = db.alloc_candidate("a", "2.0.0", {{db.alloc_requirement_from_str("b", ">=1.0.0,<4.0.0")}, {}});
    auto a_3 = db.alloc_candidate("a", "3.0.0", {{db.alloc_requirement_from_str("b", ">=4.0.0,<7.0.0")}, {}});

    auto b_1 = db.alloc_candidate("b", "8.0.0", {});
    auto b_2 = db.alloc_candidate("b", "9.0.0", {});
    auto b_3 = db.alloc_candidate("b", "10.0.0", {});

    auto c_1 = db.alloc_candidate("c", "1.0.0", {});

// Construct a problem to be solved by the solver
    resolvo::Vector<resolvo::VersionSetId> requirements = {db.alloc_requirement_from_str("a", ">=1.0.0,<3.0.0")};
    resolvo::Vector<resolvo::VersionSetId> constraints = {db.alloc_requirement_from_str("b", ">=1.0.0,<3.0.0"),
                                                          db.alloc_requirement_from_str("b", ">=1.0.0,<3.0.0")};

// Solve the problem
    resolvo::Vector<resolvo::SolvableId> result;
    auto message = resolvo::solve(db, requirements, constraints, result);

    std::cout << "message: " << message << std::endl;

}

void test_default_unsat2() {
// Construct a database with packages a, b, and c.
    PackageDatabase db;

// Construct a problem to be solved by the solver
    resolvo::Vector<resolvo::VersionSetId> requirements = {db.alloc_requirement_from_str("a", ">=1.0.0,<3.0.0")};
    resolvo::Vector<resolvo::VersionSetId> constraints = {db.alloc_requirement_from_str("b", ">=1.0.0,<3.0.0"),
                                                          db.alloc_requirement_from_str("b", ">=1.0.0,<3.0.0")};

// Solve the problem
    resolvo::Vector<resolvo::SolvableId> result;
    auto message = resolvo::solve(db, requirements, constraints, result);

    std::cout << "message: " << message << std::endl;

}

int main() {
//    test_default_sat();
//    test_default_unsat();
    test_default_unsat2();
    return 0;
}