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
#include "installer.h"

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
        for (auto solvable: result) {
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

int ttrek_ParseRequirements(Tcl_Size objc, Tcl_Obj *const objv[], std::map<std::string, std::string> &requirements) {
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
    return TCL_OK;
}

static void ttrek_ParseRequirementsFromSpecFile(ttrek_state_t *state_ptr, std::map<std::string, std::string> &requirements) {
    cJSON *dependencies = cJSON_GetObjectItem(state_ptr->spec_root, "dependencies");
    if (dependencies) {
        for (int i = 0; i < cJSON_GetArraySize(dependencies); i++) {
            cJSON *dep_item = cJSON_GetArrayItem(dependencies, i);
            std::string package_name = dep_item->string;
            std::string package_version_requirement = cJSON_GetStringValue(dep_item);
            DBG(std::cout << "(direct) package_name: " << package_name << " package_version_requirement: " << package_version_requirement << std::endl);
            requirements[package_name] = package_version_requirement;
        }
    }
}

int ttrek_Solve(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], ttrek_state_t *state_ptr, std::string& message, std::map<std::string, std::string> &requirements,
                std::vector<std::string> &installs) {
    PackageDatabase db;

    ttrek_ParseRequirements(objc, objv, requirements);
    // Parse additional requirements from spec file
    std::map<std::string, std::string> additional_requirements;
    ttrek_ParseRequirementsFromSpecFile(state_ptr, additional_requirements);

    // Construct a problem to be solved by the solver
    resolvo::Vector<resolvo::VersionSetId> requirements_vector;
    for (const auto &requirement: requirements) {
        requirements_vector.push_back(db.alloc_requirement_from_str(requirement.first, requirement.second));
    }
    for (const auto &requirement: additional_requirements) {
        requirements_vector.push_back(db.alloc_requirement_from_str(requirement.first, requirement.second));
    }

    std::map<std::string, std::string> constraints;
    // todo: constraints for optional dependencies

    resolvo::Vector<resolvo::VersionSetId> constraints_vector;
    for (const auto &constraint: constraints) {
        constraints_vector.push_back(db.alloc_requirement_from_str(constraint.first, constraint.second));
    }

    // Solve the problem
    resolvo::Vector<resolvo::SolvableId> result;
    message = resolvo::solve(db, requirements_vector, constraints_vector, result);

    if (!result.empty()) {
        for (auto solvable: result) {
            installs.emplace_back(db.display_solvable(solvable));
        }
    }

    return TCL_OK;
}

int ttrek_pretend(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], ttrek_state_t *state_ptr) {

    std::map<std::string, std::string> requirements;
    std::vector<std::string> installs;
    std::string message;
    if (TCL_OK != ttrek_Solve(interp, objc, objv, state_ptr, message, requirements, installs)) {
        return TCL_ERROR;
    }
    if (installs.empty()) {
        std::cout << message << std::endl;
    } else {
        for (const auto &install: installs) {
            std::cout << "install: " << install << std::endl;
        }
    }
    return TCL_OK;
}


static int ttrek_UpdateSpecFileAfterInstall(Tcl_Interp *interp, ttrek_state_t *state_ptr) {
    // write spec file
    if (TCL_OK != ttrek_WriteJsonFile(interp, state_ptr->spec_json_path_ptr, state_ptr->spec_root)) {
        fprintf(stderr, "error: could not write %s\n", Tcl_GetString(state_ptr->spec_json_path_ptr));
        return TCL_ERROR;
    }
    return TCL_OK;
}

static int ttrek_UpdateLockFileAfterInstall(Tcl_Interp *interp, ttrek_state_t *state_ptr) {
    // write lock file
    if (TCL_OK != ttrek_WriteJsonFile(interp, state_ptr->lock_json_path_ptr, state_ptr->lock_root)) {
        fprintf(stderr, "error: could not write %s\n", Tcl_GetString(state_ptr->lock_json_path_ptr));
        return TCL_ERROR;
    }
    return TCL_OK;
}

int ttrek_install(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], ttrek_state_t *state_ptr) {


    std::map<std::string, std::string> requirements;
    std::vector<std::string> installs;
    std::string message;

    if (TCL_OK != ttrek_Solve(interp, objc, objv, state_ptr, message, requirements, installs)) {
        return TCL_ERROR;
    }

    if (installs.empty()) {
        std::cout << message << std::endl;
    } else {

        std::reverse(installs.begin(), installs.end());
        for (const auto &install: installs) {
            std::cout << "installing... " << install << std::endl;

            auto index = install.find('='); // package_name=package_version
            auto package_name = install.substr(0, index);
            auto package_version = install.substr(index + 1);

            auto direct_version_requirement =
                    requirements.find(package_name) != requirements.end() ? requirements.at(package_name).c_str()
                                                                          : nullptr;
            if (TCL_OK !=
                ttrek_InstallPackage(interp, state_ptr, package_name.c_str(), package_version.c_str(), direct_version_requirement)) {
                return TCL_ERROR;
            }
        }

        if (TCL_OK != ttrek_UpdateSpecFileAfterInstall(interp, state_ptr)) {
            return TCL_ERROR;
        }

        if (TCL_OK != ttrek_UpdateLockFileAfterInstall(interp, state_ptr)) {
            return TCL_ERROR;
        }

    }
    return TCL_OK;
}