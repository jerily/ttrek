/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <sstream>
#include <cassert>
#include <iostream>
#include "PackageDatabase.h"
#include "ttrek_resolvo.h"

void test_default_sat() {
// Construct a database with packages a, b, and c.
    PackageDatabase db;

// Construct a problem to be solved by the solver
    resolvo::Vector<resolvo::VersionSetId> requirements = {db.alloc_requirement_from_str("a", ">=4.0.0,<5.0.0")};
//    resolvo::Vector<resolvo::VersionSetId> constraints = {db.alloc_requirement_from_str("b", ">=1.0.0,<3.0.0"),
//                                                          db.alloc_requirement_from_str("b", ">=1.0.0,<3.0.0")};
    resolvo::Vector<resolvo::VersionSetId> constraints = {};
    // Solve the problem
    resolvo::Vector<resolvo::SolvableId> result;
    auto message = resolvo::solve(db, requirements, constraints, result);

    if (!result.empty()) {
        for (auto solvable : result) {
            std::cout << "install: " << db.display_solvable(solvable) << std::endl;
        }
    } else {
        std::cout << "unsat message: " << message << std::endl;
    }

}

void test_default_unsat() {
    // Construct a database with packages a, b, and c.
    PackageDatabase db;

    // Construct a problem to be solved by the solver
    resolvo::Vector<resolvo::VersionSetId> requirements = {db.alloc_requirement_from_str("a", ">=1.0.0,<3.0.0")};
    resolvo::Vector<resolvo::VersionSetId> constraints = {db.alloc_requirement_from_str("b", ">=1.0.0,<3.0.0"),
                                                          db.alloc_requirement_from_str("b", ">=1.0.0,<3.0.0")};

    // Solve the problem
    resolvo::Vector<resolvo::SolvableId> result;
    auto message = resolvo::solve(db, requirements, constraints, result);

    std::cout << "unsat message: " << message << std::endl;

}

int test_resolvo() {
    test_default_sat();
    test_default_unsat();
    return 0;
}