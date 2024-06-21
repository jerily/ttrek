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

int ttrek_ParseConstraintsFromLockFile(Tcl_Interp *interp, std::map<std::string, std::string>& constraints) {

    Tcl_Obj *project_home_dir_ptr = ttrek_GetProjectHomeDir(interp);
    if (!project_home_dir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return TCL_ERROR;
    }

    Tcl_Obj *lock_filename_ptr = Tcl_NewStringObj(LOCK_JSON_FILE, -1);
    Tcl_IncrRefCount(lock_filename_ptr);
    Tcl_Obj *path_to_lock_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_home_dir_ptr, lock_filename_ptr, &path_to_lock_file_ptr)) {
        Tcl_DecrRefCount(lock_filename_ptr);
        Tcl_DecrRefCount(project_home_dir_ptr);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(lock_filename_ptr);
    Tcl_IncrRefCount(path_to_lock_file_ptr);

    cJSON *lock_root = nullptr;
    if (TCL_OK != ttrek_FileToJson(interp, path_to_lock_file_ptr, &lock_root)) {
        fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_to_lock_file_ptr));
        Tcl_DecrRefCount(path_to_lock_file_ptr);
        Tcl_DecrRefCount(project_home_dir_ptr);
        return TCL_ERROR;
    }

    // add constraints from direct version requirements
    cJSON *dependencies = cJSON_GetObjectItem(lock_root, "dependencies");
    if (0 && dependencies) {
        for (int i = 0; i < cJSON_GetArraySize(dependencies); i++) {
            cJSON *dep_item = cJSON_GetArrayItem(dependencies, i);
            std::string package_name = dep_item->string;
            std::string package_version_requirement = cJSON_GetStringValue(dep_item);
            std::cout << "(direct) package_name: " << package_name << " package_version_requirement: "
                      << package_version_requirement << std::endl;
            constraints[package_name] = package_version_requirement;
        }
    }

    cJSON *packages = cJSON_GetObjectItem(lock_root, "packages");
    if (packages) {
        for (int i = 0; i < cJSON_GetArraySize(packages); i++) {
            cJSON *package = cJSON_GetArrayItem(packages, i);
            cJSON *package_reqs = cJSON_GetObjectItem(package, "requires");
            for (int j = 0; j < cJSON_GetArraySize(package_reqs); j++) {
                cJSON *package_req = cJSON_GetArrayItem(package_reqs, j);
                std::string package_name = package_req->string;
                std::string package_version_requirement = cJSON_GetStringValue(package_req);
                std::cout << "(transitive) package_name: " << package_name << " package_version_requirement: "
                          << package_version_requirement << std::endl;
                constraints[package_name] = package_version_requirement;
            }
        }
    }

    Tcl_DecrRefCount(path_to_lock_file_ptr);
    Tcl_DecrRefCount(project_home_dir_ptr);
    return TCL_OK;
}

static int ttrek_ParseRequirementsFromSpecFile(Tcl_Interp *interp, std::map<std::string, std::string> &requirements) {
    cJSON *spec_root = ttrek_GetSpecRoot(interp);
    if (!spec_root) {
        fprintf(stderr, "error: getting spec root failed\n");
        return TCL_ERROR;
    }

    cJSON *dependencies = cJSON_GetObjectItem(spec_root, "dependencies");
    if (dependencies) {
        for (int i = 0; i < cJSON_GetArraySize(dependencies); i++) {
            cJSON *dep_item = cJSON_GetArrayItem(dependencies, i);
            std::string package_name = dep_item->string;
            std::string package_version_requirement = cJSON_GetStringValue(dep_item);
            std::cout << "(direct) package_name: " << package_name << " package_version_requirement: "
                      << package_version_requirement << std::endl;
            requirements[package_name] = package_version_requirement;
        }
    }
    return TCL_OK;
}

int ttrek_Solve(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], std::string& message, std::map<std::string, std::string> &requirements,
                std::vector<std::string> &installs) {
    PackageDatabase db;

    ttrek_ParseRequirements(objc, objv, requirements);
    // Parse additional requirements from spec file
    std::map<std::string, std::string> additional_requirements;
    if (TCL_OK != ttrek_ParseRequirementsFromSpecFile(interp, additional_requirements)) {
        return TCL_ERROR;
    }

    // Construct a problem to be solved by the solver
    resolvo::Vector<resolvo::VersionSetId> requirements_vector;
    for (const auto &requirement: requirements) {
        requirements_vector.push_back(db.alloc_requirement_from_str(requirement.first, requirement.second));
    }
    for (const auto &requirement: additional_requirements) {
        requirements_vector.push_back(db.alloc_requirement_from_str(requirement.first, requirement.second));
    }

    std::map<std::string, std::string> constraints;
//    if (TCL_OK != ttrek_ParseConstraintsFromLockFile(interp, constraints)) {
//        return TCL_ERROR;
//    }

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

int ttrek_pretend(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    std::map<std::string, std::string> requirements;
    std::vector<std::string> installs;
    std::string message;
    if (TCL_OK != ttrek_Solve(interp, objc, objv, message, requirements, installs)) {
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


static int ttrek_UpdateSpecFileAfterInstall(Tcl_Interp *interp, cJSON *spec_root) {

    Tcl_Obj *project_home_dir_ptr = ttrek_GetProjectHomeDir(interp);
    if (!project_home_dir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return TCL_ERROR;
    }

    // path_to_spec_file_ptr
    Tcl_Obj *spec_file_name_ptr = Tcl_NewStringObj(SPEC_JSON_FILE, -1);
    Tcl_IncrRefCount(spec_file_name_ptr);
    Tcl_Obj *path_to_spec_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_home_dir_ptr, spec_file_name_ptr, &path_to_spec_file_ptr)) {
        Tcl_DecrRefCount(spec_file_name_ptr);
        Tcl_DecrRefCount(project_home_dir_ptr);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(spec_file_name_ptr);
    Tcl_IncrRefCount(path_to_spec_file_ptr);

    // write spec file
    if (TCL_OK != ttrek_WriteJsonFile(interp, path_to_spec_file_ptr, spec_root)) {
        fprintf(stderr, "error: could not write %s\n", Tcl_GetString(path_to_spec_file_ptr));
        Tcl_DecrRefCount(path_to_spec_file_ptr);
        Tcl_DecrRefCount(project_home_dir_ptr);
        return TCL_ERROR;
    }

    Tcl_DecrRefCount(path_to_spec_file_ptr);
    Tcl_DecrRefCount(project_home_dir_ptr);
    return TCL_OK;
}

static int ttrek_UpdateLockFileAfterInstall(Tcl_Interp *interp, cJSON *lock_root) {

    Tcl_Obj *project_home_dir_ptr = ttrek_GetProjectHomeDir(interp);
    if (!project_home_dir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return TCL_ERROR;
    }

    Tcl_Obj *lock_filename_ptr = Tcl_NewStringObj(LOCK_JSON_FILE, -1);
    Tcl_IncrRefCount(lock_filename_ptr);
    Tcl_Obj *path_to_lock_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_home_dir_ptr, lock_filename_ptr, &path_to_lock_file_ptr)) {
        Tcl_DecrRefCount(lock_filename_ptr);
        Tcl_DecrRefCount(project_home_dir_ptr);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(lock_filename_ptr);
    Tcl_IncrRefCount(path_to_lock_file_ptr);

    // write lock file
    if (TCL_OK != ttrek_WriteJsonFile(interp, path_to_lock_file_ptr, lock_root)) {
        fprintf(stderr, "error: could not write %s\n", Tcl_GetString(path_to_lock_file_ptr));
        Tcl_DecrRefCount(path_to_lock_file_ptr);
        Tcl_DecrRefCount(project_home_dir_ptr);
        return TCL_ERROR;
    }

    Tcl_DecrRefCount(path_to_lock_file_ptr);
    Tcl_DecrRefCount(project_home_dir_ptr);
    return TCL_OK;
}

int ttrek_install(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {


    std::map<std::string, std::string> requirements;
    std::vector<std::string> installs;
    std::string message;

    if (TCL_OK != ttrek_Solve(interp, objc, objv, message, requirements, installs)) {
        return TCL_ERROR;
    }

    if (installs.empty()) {
        std::cout << message << std::endl;
    } else {

        cJSON *spec_root = ttrek_GetSpecRoot(interp);
        if (!spec_root) {
            fprintf(stderr, "error: getting spec root failed\n");
            return TCL_ERROR;
        }

        cJSON *lock_root = ttrek_GetLockRoot(interp);
        if (!lock_root) {
            fprintf(stderr, "error: getting lock root failed\n");
            return TCL_ERROR;
        }

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
                ttrek_InstallPackage(interp, package_name.c_str(), package_version.c_str(), direct_version_requirement,
                                     spec_root, lock_root)) {
                return TCL_ERROR;
            }
        }

        if (TCL_OK != ttrek_UpdateSpecFileAfterInstall(interp, spec_root)) {
            cJSON_free(spec_root);
            cJSON_free(lock_root);
            return TCL_ERROR;
        }

        if (TCL_OK != ttrek_UpdateLockFileAfterInstall(interp, lock_root)) {
            cJSON_free(spec_root);
            cJSON_free(lock_root);
            return TCL_ERROR;
        }

        cJSON_free(spec_root);
        cJSON_free(lock_root);

    }
    return TCL_OK;
}