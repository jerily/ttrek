/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <sstream>
#include <cassert>
#include <iostream>
#include <cstring>
#include "PackageDatabase.h"
#include "ttrek_resolvo.h"
#include "installer.h"

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
        DBG(std::cout << "package_name: " << package_name << " version_requirement: " << version_requirement
                      << std::endl);
        requirements[package_name] = version_requirement;
    }
    return TCL_OK;
}

void ttrek_ParseLockedPackages(ttrek_state_t *state_ptr, PackageDatabase &db) {
    cJSON *packages = cJSON_GetObjectItem(state_ptr->lock_root, "packages");
    if (packages) {
        for (int i = 0; i < cJSON_GetArraySize(packages); i++) {
            cJSON *package = cJSON_GetArrayItem(packages, i);
            std::string package_name = package->string;
            std::string package_version = cJSON_GetStringValue(cJSON_GetObjectItem(package, "version"));
            db.alloc_locked_package(package_name, package_version);
        }
    }
}

static void ttrek_ParseReverseDependencies(cJSON *lock_root,
                                           std::map<std::string, std::vector<ReverseDependency>> &reverse_dependencies) {

    cJSON *packages = cJSON_GetObjectItem(lock_root, "packages");
    if (!packages) {
        return;
    }

    for (int i = 0; i < cJSON_GetArraySize(packages); i++) {
        cJSON *package = cJSON_GetArrayItem(packages, i);
        std::string package_name = package->string;
        std::string package_version = cJSON_GetStringValue(cJSON_GetObjectItem(package, "version"));
        if (!cJSON_HasObjectItem(package, "requires")) {
            continue;
        }
        cJSON *dependencies = cJSON_GetObjectItem(package, "requires");
        for (int j = 0; j < cJSON_GetArraySize(dependencies); j++) {
            cJSON *dep_item = cJSON_GetArrayItem(dependencies, j);
            std::string dep_package_name = dep_item->string;
            if (reverse_dependencies.find(dep_package_name) == reverse_dependencies.end()) {
                reverse_dependencies[dep_package_name] = std::vector<ReverseDependency>();
            }
            reverse_dependencies[dep_package_name].push_back(ReverseDependency{package_name, package_version});
        }
    }
}

int
ttrek_Solve(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], PackageDatabase &db, ttrek_state_t *state_ptr,
            std::string &message,
            std::map<std::string, std::string> &requirements,
            std::vector<std::string> &installs) {

    ttrek_ParseRequirements(objc, objv, requirements);
    // Parse additional requirements from spec file
    std::map<std::string, std::string> existing_requirements;
//    ttrek_ParseRequirementsFromSpecFile(state_ptr, existing_requirements);

    ttrek_ParseLockedPackages(state_ptr, db);

    // Construct a problem to be solved by the solver
    resolvo::Vector<resolvo::VersionSetId> requirements_vector;
    for (const auto &requirement: existing_requirements) {
        requirements_vector.push_back(db.alloc_requirement_from_str(requirement.first, requirement.second));
    }
    for (const auto &requirement: requirements) {
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
        db.topological_sort(installs);
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

static int ttrek_ExistsInLock(cJSON *lock_root, const char *package_name, const char *package_version,
                              int *package_name_exists_in_lock_p) {

    *package_name_exists_in_lock_p = 0;

    cJSON *packages = cJSON_GetObjectItem(lock_root, "packages");
    if (!packages) {
        return 0;
    }

    cJSON *package = cJSON_GetObjectItem(packages, package_name);
    if (!package) {
        return 0;
    }

    *package_name_exists_in_lock_p = 1;

    cJSON *version = cJSON_GetObjectItem(package, "version");
    if (!version) {
        return 0;
    }

    if (strcmp(version->valuestring, package_version) != 0) {
        return 0;
    }

    return 1;
}

typedef enum {
    DIRECT_INSTALL,
    RDEP_INSTALL
} ttrek_install_type_t;

struct InstallSpec {
    ttrek_install_type_t install_type;
    std::string package_name;
    std::string package_version;
    std::string direct_version_requirement;
    int package_name_exists_in_lock_p;
};

static void ttrek_AddInstallToExecutionPlan(PackageDatabase &db, ttrek_state_t *state_ptr, const std::string &install,
                                            std::map<std::string, std::string> &requirements,
                                            std::vector<InstallSpec> &execution_plan) {
    auto index = install.find('='); // package_name=package_version
    auto package_name = install.substr(0, index);
    auto package_version = install.substr(index + 1);

    int package_name_exists_in_lock_p;
    int exact_package_exists_in_lock_p = ttrek_ExistsInLock(state_ptr->lock_root, package_name.c_str(),
                                                            package_version.c_str(),
                                                            &package_name_exists_in_lock_p);

    auto reverse_dependency_p = false;
    if (exact_package_exists_in_lock_p && db.is_rdep(package_name)) {
        reverse_dependency_p = true;
    }

    if (!state_ptr->option_force && !reverse_dependency_p && exact_package_exists_in_lock_p) {
        fprintf(stderr, "info: %s@%s already installed\n", package_name.c_str(), package_version.c_str());
        return;
    }

    auto direct_version_requirement =
            requirements.find(package_name) != requirements.end() ? requirements.at(package_name)
                                                                  : "";

    auto install_spec = InstallSpec{reverse_dependency_p ? RDEP_INSTALL : DIRECT_INSTALL, package_name, package_version,
                                    direct_version_requirement, package_name_exists_in_lock_p};
    execution_plan.push_back(install_spec);
}

static void ttrek_CleanupReverseDependencies(PackageDatabase &db, std::vector<InstallSpec> &execution_plan) {

    // get the set of direct installs
    std::unordered_set<std::string> filtered_installs;
    for (const auto &install: execution_plan) {
        if (install.install_type == DIRECT_INSTALL) {
            filtered_installs.insert(install.package_name);
        }
    }

    // get the set of reverse dependencies that are reverse dependencies of direct installs or transitive
    auto changed = true;
    do {
        changed = false;
        for (const auto &spec: execution_plan) {
            if (spec.install_type == RDEP_INSTALL && filtered_installs.find(spec.package_name) == filtered_installs.end()) {
                auto deps = db.get_dependent_package_names(spec.package_name);
                bool found = false;
                for (const auto &dep: deps) {
                    if (filtered_installs.find(dep) != filtered_installs.end()) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    filtered_installs.insert(spec.package_name);
                    changed = true;
                }
            }
        }
    } while (changed);

    // remove reverse dependencies that are not reverse dependencies of direct installs or transitive
    execution_plan.erase(std::remove_if(execution_plan.begin(), execution_plan.end(),
                                        [&filtered_installs](const InstallSpec &spec) -> bool {
                                            return spec.install_type == RDEP_INSTALL &&
                                                   filtered_installs.find(spec.package_name) == filtered_installs.end();
                                        }), execution_plan.end());
}

static void
ttrek_GenerateExecutionPlan(PackageDatabase &db, ttrek_state_t *state_ptr, const std::vector<std::string> &installs,
                            std::map<std::string, std::string> &requirements,
                            std::vector<InstallSpec> &execution_plan) {

    // add installs to execution plan
    for (const auto &install: installs) {
        ttrek_AddInstallToExecutionPlan(db, state_ptr, install, requirements, execution_plan);
    }

    // now go through the execution plan and remove all reverse dependencies
    // that are not reverse dependencies of direct installs
    ttrek_CleanupReverseDependencies(db, execution_plan);
}

int ttrek_InstallOrUpdate(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], ttrek_state_t *state_ptr) {

    PackageDatabase db;
    db.set_strategy(state_ptr->strategy);

    std::map<std::string, std::vector<ReverseDependency>> reverse_dependencies_map;
    ttrek_ParseReverseDependencies(state_ptr->lock_root, reverse_dependencies_map);
    db.set_reverse_dependencies_map(reverse_dependencies_map);


    std::map<std::string, std::string> requirements;
    std::vector<std::string> installs;
    std::string message;

    if (TCL_OK != ttrek_Solve(interp, objc, objv, db, state_ptr, message, requirements, installs)) {
        return TCL_ERROR;
    }

    if (installs.empty()) {
        std::cout << message << std::endl;
    } else {

        // generate the execution plan
        std::vector<InstallSpec> execution_plan;
        ttrek_GenerateExecutionPlan(db, state_ptr, installs, requirements, execution_plan);

        // print the execution plan

        if (execution_plan.empty()) {
            std::cout << "Nothing to install!" << std::endl;
            return TCL_OK;
        }

        std::cout << "The following packages will be installed:" << std::endl;
        for (const auto &install_spec: execution_plan) {
            std::cout << install_spec.package_name << "@" << install_spec.package_version;
            if (install_spec.install_type == RDEP_INSTALL) {
                std::cout << " (reverse dependency)";
            }
            std::cout << std::endl;
        }


        if (!state_ptr->option_yes) {
            // get yes/no from user
            std::string answer;
            std::cout << "Do you want to proceed? [y/N] ";
            std::getline(std::cin, answer);
            if (answer != "y") {
                return TCL_OK;
            }
        }

        // perform the installation
        std::vector<InstallSpec> installs_from_lock_file_sofar;
        for (const auto &install_spec: execution_plan) {
            auto package_name = install_spec.package_name;
            auto package_version = install_spec.package_version;
            auto direct_version_requirement = install_spec.direct_version_requirement;
            auto package_name_exists_in_lock_p = install_spec.package_name_exists_in_lock_p;

            std::cout << "installing... " << package_name << "@" << package_version << std::endl;

            if (TCL_OK !=
                ttrek_InstallPackage(interp, state_ptr, package_name.c_str(), package_version.c_str(),
                                     direct_version_requirement.c_str(), package_name_exists_in_lock_p)) {

                for (const auto &spec: installs_from_lock_file_sofar) {
                    if (package_name_exists_in_lock_p) {
                        fprintf(stderr, "restoring package files from old installation: %s\n", spec.package_name.c_str());
                        if (TCL_OK != ttrek_RestoreTempFiles(interp, state_ptr, spec.package_name.c_str())) {
                            fprintf(stderr, "error: could not restore package files from old installation\n");
                        }
                    }
                }

                return TCL_ERROR;
            }

            if (package_name_exists_in_lock_p) {
                installs_from_lock_file_sofar.push_back(install_spec);
            }
        }

        if (TCL_OK != ttrek_UpdateSpecFileAfterInstall(interp, state_ptr)) {
            return TCL_ERROR;
        }

        if (TCL_OK != ttrek_UpdateLockFileAfterInstall(interp, state_ptr)) {
            return TCL_ERROR;
        }

        for (const auto &install_spec: installs_from_lock_file_sofar) {
            ttrek_DeleteTempFiles(interp, state_ptr, install_spec.package_name.c_str());
        }

    }
    return TCL_OK;
}

int ttrek_Uninstall(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], ttrek_state_t *state_ptr) {

    std::map<std::string, std::vector<ReverseDependency>> reverse_dependencies_map;
    ttrek_ParseReverseDependencies(state_ptr->lock_root, reverse_dependencies_map);

    std::unordered_set<std::string> uninstalls;

    // prepare the initial list of packages to uninstall
    for (Tcl_Size i = 0; i < objc; i++) {
        std::string package_name = Tcl_GetString(objv[i]);
        uninstalls.insert(package_name);
    }

    // prepare the list of reverse dependencies to uninstall
    bool changed;
    do {
        changed = false;
        for (const auto &uninstall: uninstalls) {
            auto it = reverse_dependencies_map.find(uninstall);
            if (it != reverse_dependencies_map.end()) {
                for (const auto &rdep: it->second) {
                    if (uninstalls.find(rdep.package_name) == uninstalls.end()) {
                        uninstalls.insert(rdep.package_name);
                        changed = true;
                    }
                }
            }
        }
    } while (changed);

    // print the list of packages to uninstall
    std::cout << "The following packages will be uninstalled:" << std::endl;
    for (const auto &uninstall: uninstalls) {
        std::cout << uninstall << std::endl;
    }

    // get user confirmation
    if (!state_ptr->option_yes) {
        std::string answer;
        std::cout << "Do you want to proceed? [y/N] ";
        std::getline(std::cin, answer);
        if (answer != "y") {
            return TCL_OK;
        }
    }

    for (const auto &uninstall: uninstalls) {
        if (TCL_OK != ttrek_UninstallPackage(interp, state_ptr, uninstall.c_str())) {
            return TCL_ERROR;
        }
    }

    // update spec and lock files
    if (TCL_OK != ttrek_UpdateSpecFileAfterInstall(interp, state_ptr)) {
        fprintf(stderr, "error: could not update spec file\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_UpdateLockFileAfterInstall(interp, state_ptr)) {
        fprintf(stderr, "error: could not update lock file\n");
        return TCL_ERROR;
    }

    return TCL_OK;
}
