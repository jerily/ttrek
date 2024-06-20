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
    resolvo::Vector<resolvo::VersionSetId> requirements = {db.alloc_requirement_from_str("a", ">=4.0.0,<6.0.0")};
    resolvo::Vector<resolvo::VersionSetId> constraints = {db.alloc_requirement_from_str("b", ">=1.0.0,<13.0.0"),
                                                          db.alloc_requirement_from_str("b", ">=1.0.0,<13.0.0")};
//    resolvo::Vector<resolvo::VersionSetId> constraints = {};
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
    std::cout << "----------------------------" << std::endl;
    std::cout << "test_default_sat" << std::endl;
    test_default_sat();
    std::cout << "----------------------------" << std::endl;
    std::cout << "test_default_unsat" << std::endl;
    test_default_unsat();
    return 0;
}

int ttrek_pretend(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    PackageDatabase db;

    // split all the arguments in "objv" into a vector of package names and the version requirements
    // each argument is of the form "package_name@version_requirement"

    std::map<std::string, std::string> requirements;
    for (int i = 0; i < objc; i++) {
        std::string arg = Tcl_GetString(objv[i]);
        std::string package_name;
        std::string version_requirement;
        if (arg.find('@') == std::string::npos) {
            package_name = arg;
            version_requirement = "";
        } else {
            package_name = arg.substr(0, arg.find('@'));
            version_requirement = arg.substr(arg.find('@') + 1);
        }
        std::cout << "package_name: " << package_name << " version_requirement: " << version_requirement << std::endl;
        requirements[package_name] = version_requirement;
    }

    // Construct a problem to be solved by the solver
    resolvo::Vector<resolvo::VersionSetId> requirements_vector;
    for (const auto& requirement : requirements) {
        requirements_vector.push_back(db.alloc_requirement_from_str(requirement.first, requirement.second));
    }

    resolvo::Vector<resolvo::VersionSetId> constraints;

    // Solve the problem
    resolvo::Vector<resolvo::SolvableId> result;
    auto message = resolvo::solve(db, requirements_vector, constraints, result);

    if (!result.empty()) {
        for (auto solvable : result) {
            std::cout << "install: " << db.display_solvable(solvable) << std::endl;
        }
    } else {
        std::cout << "unsat message: " << message << std::endl;
    }

    return TCL_OK;
}