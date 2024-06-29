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
        DBG(std::cout << "package_name: " << package_name << " version_requirement: " << version_requirement << std::endl);
        requirements[package_name] = version_requirement;
    }
    return TCL_OK;
}

static void
ttrek_ParseRequirementsFromSpecFile(ttrek_state_t *state_ptr, std::map<std::string, std::string> &requirements) {
    cJSON *dependencies = cJSON_GetObjectItem(state_ptr->spec_root, "dependencies");
    if (dependencies) {
        for (int i = 0; i < cJSON_GetArraySize(dependencies); i++) {
            cJSON *dep_item = cJSON_GetArrayItem(dependencies, i);
            std::string package_name = dep_item->string;
            std::string package_version_requirement = cJSON_GetStringValue(dep_item);
            DBG(std::cout << "(direct) package_name: " << package_name << " package_version_requirement: "
                          << package_version_requirement << std::endl);
            requirements[package_name] = package_version_requirement;
        }
    }
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

int
ttrek_Solve(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], ttrek_state_t *state_ptr, std::string &message,
            std::map<std::string, std::string> &requirements,
            std::vector<std::string> &installs) {
    PackageDatabase db;
    db.set_strategy(state_ptr->strategy);

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

static int
ttrek_HasChangedDependencyVersion(cJSON *lock_root, const std::map<std::string, std::string> &installed_packages_map,
                                  const char *package_name, const char *package_version) {
    cJSON *packages = cJSON_GetObjectItem(lock_root, "packages");
    if (!packages) {
        return 0;
    }

    cJSON *package = cJSON_GetObjectItem(packages, package_name);
    if (!package) {
        return 0;
    }

    cJSON *version = cJSON_GetObjectItem(package, "version");
    if (!version) {
        return 0;
    }

    if (strcmp(version->valuestring, package_version) != 0) {
        return 0;
    }

    cJSON *dependencies = cJSON_GetObjectItem(package, "requires");
    if (!dependencies) {
        return 0;
    }

    for (int i = 0; i < cJSON_GetArraySize(dependencies); i++) {
        cJSON *dep_item = cJSON_GetArrayItem(dependencies, i);
        std::string dep_package_name = dep_item->string;
        std::string dep_package_version = cJSON_GetStringValue(dep_item);
        if (installed_packages_map.find(dep_package_name) != installed_packages_map.end()) {
            if (installed_packages_map.at(dep_package_name) != dep_package_version) {
                return 1;
            }
        }
    }

    return 0;
}

struct ReverseDependency {
    std::string package_name;
    std::string package_version;
};

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
        cJSON *dependencies = cJSON_GetObjectItem(package, "requires");
        if (!dependencies) {
            continue;
        }
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

typedef enum {
    DIRECT_INSTALL,
    RDEP_INSTALL
} ttrek_install_type_t;

struct InstallSpec {
    ttrek_install_type_t install_type;
    std::string package_name;
    std::string package_version;
    const char *direct_version_requirement;
    int package_name_exists_in_lock_p;
};

static void ttrek_ComputeReverseDependencies(const std::vector<std::string> &installs,
                                             const std::map<std::string, std::vector<ReverseDependency>> &reverse_dependencies,
                                             std::vector<std::string> &rdep_installs) {

    auto list_of_packages = installs;
    bool changed = false;
    while (rdep_installs.empty() || changed) {
        changed = false;
        for (const auto &install: list_of_packages) {
            auto index = install.find('='); // package_name=package_version
            auto package_name = install.substr(0, index);
            auto package_version = install.substr(index + 1);
            if (reverse_dependencies.find(package_name) != reverse_dependencies.end()) {
                for (const auto &rdep: reverse_dependencies.at(package_name)) {
                    std::string rdep_install = rdep.package_name + "=" + rdep.package_version;
                    if (std::find(rdep_installs.begin(), rdep_installs.end(), rdep_install) == rdep_installs.end()) {
                        std::cout << "adding rdep... " << rdep_install << " due to " << package_name << std::endl;
                        // if not already in the list, add the direct reverse dependency
                        rdep_installs.push_back(rdep_install);

                        changed = true;
                    }
                }
            }
        }

        if (!changed) {
            break;
        }
        list_of_packages = rdep_installs;
    };
}

static void ttrek_AddInstallToExecutionPlan(ttrek_state_t *state_ptr, const std::string &install, std::map<std::string, std::string> &requirements,
                                            std::vector<InstallSpec> &execution_plan) {
    auto index = install.find('='); // package_name=package_version
    auto package_name = install.substr(0, index);
    auto package_version = install.substr(index + 1);


    auto direct_version_requirement =
            requirements.find(package_name) != requirements.end() ? requirements.at(package_name).c_str()
                                                                  : nullptr;

    int package_name_exists_in_lock_p;
    int exact_package_exists_in_lock_p = ttrek_ExistsInLock(state_ptr->lock_root, package_name.c_str(),
                                                            package_version.c_str(),
                                                            &package_name_exists_in_lock_p);

    if (!state_ptr->option_force && exact_package_exists_in_lock_p) {
        fprintf(stderr, "info: %s@%s already installed\n", package_name.c_str(), package_version.c_str());
        return;
    }

    auto install_spec = InstallSpec{DIRECT_INSTALL, package_name, package_version, direct_version_requirement, package_name_exists_in_lock_p};
    execution_plan.push_back(install_spec);
}

static void ttrek_AddReverseDependencyToExecutionPlan(ttrek_state_t *state_ptr, const std::string &rdep_install, std::map<std::string, std::string> &requirements,
                                                     std::vector<InstallSpec> &execution_plan) {
    auto index = rdep_install.find('='); // package_name=package_version
    auto package_name = rdep_install.substr(0, index);
    auto package_version = rdep_install.substr(index + 1);

    auto direct_version_requirement =
            requirements.find(package_name) != requirements.end() ? requirements.at(package_name).c_str()
                                                                  : nullptr;

    int package_name_exists_in_lock_p;
    int exact_package_exists_in_lock_p = ttrek_ExistsInLock(state_ptr->lock_root, package_name.c_str(),
                                                            package_version.c_str(),
                                                            &package_name_exists_in_lock_p);

    if (exact_package_exists_in_lock_p) {
        // we deliberately reinstall in this case
    }

    auto install_spec = InstallSpec{RDEP_INSTALL, package_name, package_version, direct_version_requirement, package_name_exists_in_lock_p};
    execution_plan.push_back(install_spec);
}

static void ttrek_GenerateExecutionPlan(ttrek_state_t *state_ptr, const std::vector<std::string> &installs,
                                       std::map<std::string, std::string> &requirements,
                                       std::vector<InstallSpec> &execution_plan) {

    std::vector<std::string> filtered_installs;
    std::copy_if(installs.begin(), installs.end(), std::back_inserter(filtered_installs), [&state_ptr,&requirements](const std::string &install) -> bool {
        auto index = install.find('='); // package_name=package_version
        auto package_name = install.substr(0, index);
        auto package_version = install.substr(index + 1);
        int package_name_exists_in_lock_p;
        return (state_ptr->option_force && requirements.find(package_name) != requirements.end()) || !ttrek_ExistsInLock(state_ptr->lock_root, package_name.c_str(), package_version.c_str(), &package_name_exists_in_lock_p);
    });

    // get all reverse dependencies from lock file
    std::map<std::string, std::vector<ReverseDependency>> reverse_dependencies;
    ttrek_ParseReverseDependencies(state_ptr->lock_root, reverse_dependencies);

    // compute reverse dependencies for all installs
    std::vector<std::string> rdep_installs;
    ttrek_ComputeReverseDependencies(filtered_installs, reverse_dependencies, rdep_installs);

    // add installs to execution plan
    for (const auto &install: filtered_installs) {
        ttrek_AddInstallToExecutionPlan(state_ptr, install, requirements, execution_plan);
    }

    // add reverse dependencies to execution plan
    for (const auto &install: rdep_installs) {
        ttrek_AddReverseDependencyToExecutionPlan(state_ptr, install, requirements, execution_plan);
    }
}

int ttrek_InstallOrUpdate(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], ttrek_state_t *state_ptr) {


    std::map<std::string, std::string> requirements;
    std::vector<std::string> installs;
    std::string message;

    if (TCL_OK != ttrek_Solve(interp, objc, objv, state_ptr, message, requirements, installs)) {
        return TCL_ERROR;
    }

    if (installs.empty()) {
        std::cout << message << std::endl;
    } else {

        // generate the execution plan
        std::vector<InstallSpec> execution_plan;
        ttrek_GenerateExecutionPlan(state_ptr, installs, requirements, execution_plan);

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
        for (const auto &install_spec: execution_plan) {
            auto package_name = install_spec.package_name;
            auto package_version = install_spec.package_version;
            auto direct_version_requirement = install_spec.direct_version_requirement;
            auto package_name_exists_in_lock_p = install_spec.package_name_exists_in_lock_p;

            std::cout << "installing... " << package_name << "@" << package_version << std::endl;

            if (TCL_OK !=
                ttrek_InstallPackage(interp, state_ptr, package_name.c_str(), package_version.c_str(),
                                     direct_version_requirement, package_name_exists_in_lock_p)) {
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