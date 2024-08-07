/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <sstream>
#include <cassert>
#include <iostream>
#include <cstring>
#include <sys/utsname.h>
#include "PackageDatabase.h"
#include "ttrek_resolvo.h"
#include "installer.h"
#include "ttrek_telemetry.h"
#include "ttrek_useflags.h"

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

static void
ttrek_ParseRequirementsFromSpecFile(ttrek_state_t *state_ptr, std::map<std::string, std::string> &requirements) {
    cJSON *dependencies = cJSON_GetObjectItem(state_ptr->spec_root, "dependencies");
    if (dependencies) {
        for (int i = 0; i < cJSON_GetArraySize(dependencies); i++) {
            cJSON *dep_item = cJSON_GetArrayItem(dependencies, i);
            std::string package_name = dep_item->string;
            std::string package_version_requirement = cJSON_GetStringValue(dep_item);
            DBG(std::cout << "(direct from spec) package_name: " << package_name << " package_version_requirement: "
                          << package_version_requirement << std::endl);
            requirements[package_name] = package_version_requirement;
        }
    }
}

static void
ttrek_ParseRequirementsFromLockFile(ttrek_state_t *state_ptr, std::map<std::string, std::string> &requirements) {
    cJSON *packages = cJSON_GetObjectItem(state_ptr->lock_root, "packages");
    if (packages) {
        for (int i = 0; i < cJSON_GetArraySize(packages); i++) {
            cJSON *dep_item = cJSON_GetArrayItem(packages, i);
            std::string package_name = dep_item->string;
            requirements[package_name] = "";
            DBG(std::cout << "(direct from lock without version) package_name: " << package_name << std::endl);
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

static void ttrek_ParseReverseDependenciesFromLock(cJSON *lock_root,
                                                   std::map<std::string, std::unordered_set<std::string>> &reverse_dependencies_map) {

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
            if (reverse_dependencies_map.find(dep_package_name) == reverse_dependencies_map.end()) {
                reverse_dependencies_map[dep_package_name] = std::unordered_set<std::string>();
            }
            reverse_dependencies_map[dep_package_name].insert(package_name);
        }
    }
}

static void ttrek_ParseDependenciesFromLock(cJSON *lock_root,
                                            std::map<std::string, std::unordered_set<std::string>> &dependencies_map) {

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
            if (dependencies_map.find(package_name) == dependencies_map.end()) {
                dependencies_map[package_name] = std::unordered_set<std::string>();
            }
            dependencies_map[package_name].insert(dep_package_name);
        }
    }
}

static void
ttrek_ParseUseFlagsFromLockFile(cJSON *lock_root,
                                std::map<std::string, std::set<UseFlag>> &iuse_flags_map,
                                std::map<std::string, std::set<UseFlag>> &use_flags_map) {

    cJSON *packages = cJSON_GetObjectItem(lock_root, "packages");
    if (!packages) {
        return;
    }

    for (int i = 0; i < cJSON_GetArraySize(packages); i++) {
        cJSON *package = cJSON_GetArrayItem(packages, i);
        std::string package_name = package->string;

        std::set<UseFlag> iuse_flags;
        if (cJSON_HasObjectItem(package, "iuse")) {
            cJSON *iuse = cJSON_GetObjectItem(package, "iuse");
            for (int j = 0; j < cJSON_GetArraySize(iuse); j++) {
                cJSON *iuse_item = cJSON_GetArrayItem(iuse, j);
                std::string iuse_flag_str = iuse_item->valuestring;
                UseFlag iuse_flag(iuse_flag_str);
                iuse_flags.insert(iuse_flag);
            }
        }
        iuse_flags_map[package_name] = iuse_flags;

        std::set<UseFlag> use_flags;
        if (cJSON_HasObjectItem(package, "use")) {
            cJSON *use = cJSON_GetObjectItem(package, "use");
            for (int j = 0; j < cJSON_GetArraySize(use); j++) {
                cJSON *use_item = cJSON_GetArrayItem(use, j);
                std::string use_flag_str = use_item->valuestring;
                UseFlag use_flag(use_flag_str);
                use_flags.insert(use_flag);
            }
        }
        use_flags_map[package_name] = use_flags;

    }

}

static void ttrek_ParseUseFlagsFromSpecFile(ttrek_state_t *state_ptr, std::unordered_set<UseFlag> &use_flags) {
    cJSON *use = cJSON_GetObjectItem(state_ptr->spec_root, "useFlags");
    if (use) {
        for (int i = 0; i < cJSON_GetArraySize(use); i++) {
            cJSON *use_item = cJSON_GetArrayItem(use, i);
            use_flags.insert(UseFlag(use_item->valuestring));
        }
    }
}

int
ttrek_Solve(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], PackageDatabase &db, ttrek_state_t *state_ptr,
            std::string &message,
            std::map<std::string, std::string> &requirements,
            std::vector<std::string> &installs) {

    UNUSED(interp);

    // Parse additional requirements from spec file
    ttrek_ParseRequirementsFromLockFile(state_ptr, requirements);
    ttrek_ParseRequirementsFromSpecFile(state_ptr, requirements);
    ttrek_ParseRequirements(objc, objv, requirements);

    ttrek_ParseLockedPackages(state_ptr, db);

    std::unordered_set<UseFlag> use_flags;
    ttrek_ParseUseFlagsFromSpecFile(state_ptr, use_flags);

    // Construct a problem to be solved by the solver
    resolvo::Vector<resolvo::VersionSetId> requirements_vector;
    for (const auto &requirement: requirements) {
        requirements_vector.push_back(db.alloc_requirement_from_str(requirement.first, requirement.second));
    }

    for (const auto &use_flag: use_flags) {
        requirements_vector.push_back(db.alloc_requirement_from_use_flag(use_flag));

        // alloc_candidate for both polarities, no deps
        auto use_flag_str = "use:" + use_flag.name;
        db.alloc_candidate(use_flag_str, "0.0.0", resolvo::Dependencies());
        db.alloc_candidate(use_flag_str, "1.2.3", resolvo::Dependencies());

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

        // print result
//        std::cout << "The following packages will be installed:" << std::endl;
//        for (const auto &install: installs) {
//            std::cout << install << std::endl;
//        }

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

static int ttrek_UpdateManifestFileAfterInstall(Tcl_Interp *interp, ttrek_state_t *state_ptr) {
    // write lock file
    if (TCL_OK != ttrek_WriteJsonFile(interp, state_ptr->manifest_json_path_ptr, state_ptr->manifest_root)) {
        fprintf(stderr, "error: could not write %s\n", Tcl_GetString(state_ptr->manifest_json_path_ptr));
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
    UNKNOWN_INSTALL,
    DIRECT_INSTALL,
    RDEP_INSTALL,
    DEP_INSTALL,
    ALREADY_INSTALLED
} ttrek_install_type_t;

struct InstallSpec {
    ttrek_install_type_t install_type;
    std::string package_name;
    std::string package_version;
    std::string direct_version_requirement;
    int package_name_exists_in_lock_p;
    int exact_package_exists_in_lock_p;
    int exact_use_flags_p;
};

static bool ttrek_HashTableCompareUseFlagsEqual(Tcl_Interp *interp, Tcl_HashTable *global_use_flags_ht_ptr,
                                                std::set<UseFlag> &iuse_flags,
                                                std::set<UseFlag> &use_flags) {

    // iterate over the hashtable use_flags_ht_ptr
    // for each entry, check if it is in iuse_flags
    // if it is, check if the value also exists in use_flags

    // first check all global use flags that are in iuse flags are also in use flags
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    for (entry = Tcl_FirstHashEntry(global_use_flags_ht_ptr, &search);
         entry != nullptr;
         entry = Tcl_NextHashEntry(&search)) {
        const char *use_flag_name = (const char *) Tcl_GetHashKey(global_use_flags_ht_ptr, entry);
        int use_flag_polarity = PTR2INT(Tcl_GetHashValue(entry));
        auto use_flag = UseFlag(use_flag_name, use_flag_polarity);
        if (iuse_flags.find(use_flag) != iuse_flags.end()) {
            if (use_flags.find(use_flag) == use_flags.end()) {
                // if the global use flag exists in iuse flags
                // but it does not exist in the use flags used previously
                // to build the package then return false
                return false;
            }
        }
    }

    // then check all use flags are in global use flags
    for (auto &use_flag: use_flags) {
        const char *use_flag_name = use_flag.name.c_str();
        int use_flag_polarity = use_flag.polarity;
        entry = Tcl_FindHashEntry(global_use_flags_ht_ptr, use_flag_name);
        if (entry == nullptr) {
            // if the use flag does not exist in the global use flags
            // then return false
            return false;
        }
        if (PTR2INT(Tcl_GetHashValue(entry)) != use_flag_polarity) {
            // if the use flag exists in the global use flags
            // but the polarity is different then return false
            return false;
        }
    }

    return true;
}

static void ttrek_AddInstallToExecutionPlan(ttrek_state_t *state_ptr, const std::string &install,
                                            const std::map<std::string, std::string> &requirements,
                                            Tcl_HashTable *global_use_flags_ht_ptr,
                                            std::map<std::string, std::set<UseFlag>> &iuse_flags_map,
                                            std::map<std::string, std::set<UseFlag>> &use_flags_map,
                                            std::vector<InstallSpec> &execution_plan) {
    auto index = install.find('='); // package_name=package_version
    auto package_name = install.substr(0, index);
    auto package_version = install.substr(index + 1);

    int package_name_exists_in_lock_p;
    int exact_package_exists_in_lock_p = ttrek_ExistsInLock(state_ptr->lock_root, package_name.c_str(),
                                                            package_version.c_str(),
                                                            &package_name_exists_in_lock_p);

    bool exact_use_flags_p = ttrek_HashTableCompareUseFlagsEqual(state_ptr->interp, global_use_flags_ht_ptr,
                                                                 iuse_flags_map[package_name],
                                                                 use_flags_map[package_name]);

    int in_requirements_p = requirements.find(package_name) != requirements.end();
    auto direct_version_requirement = in_requirements_p ? requirements.at(package_name) : "none";

    ttrek_install_type_t install_type;
    if (state_ptr->option_force) {
        install_type = DIRECT_INSTALL;
    } else if (in_requirements_p && (!exact_package_exists_in_lock_p || !exact_use_flags_p)) {
        install_type = DIRECT_INSTALL;
    } else {
        install_type = UNKNOWN_INSTALL;
    }
    auto install_spec = InstallSpec{
            install_type,
            package_name,
            package_version,
            direct_version_requirement,
            package_name_exists_in_lock_p,
            exact_package_exists_in_lock_p,
            exact_use_flags_p};
    execution_plan.push_back(install_spec);
}

static void
ttrek_GenerateExecutionPlan(ttrek_state_t *state_ptr, const std::vector<std::string> &installs,
                            const std::map<std::string, std::string> &requirements,
                            const std::map<std::string, std::unordered_set<std::string>> &dependencies_from_solver_map,
                            Tcl_HashTable *global_use_flags_ht_ptr,
                            std::vector<InstallSpec> &execution_plan) {

    std::map<std::string, std::unordered_set<std::string>> reverse_dependencies_map;
    ttrek_ParseReverseDependenciesFromLock(state_ptr->lock_root, reverse_dependencies_map);

    std::map<std::string, std::unordered_set<std::string>> dependencies_map;
    ttrek_ParseDependenciesFromLock(state_ptr->lock_root, dependencies_map);
    for (const auto &dependency: dependencies_from_solver_map) {
        dependencies_map[dependency.first] = dependency.second;
    }

    std::map<std::string, std::string> enhanced_requirements;
    ttrek_ParseRequirementsFromSpecFile(state_ptr, enhanced_requirements);
    for (const auto &requirement: requirements) {
        enhanced_requirements[requirement.first] = requirement.second;
    }

    std::map<std::string, std::set<UseFlag>> iuse_flags_map;
    std::map<std::string, std::set<UseFlag>> use_flags_map;
    ttrek_ParseUseFlagsFromLockFile(state_ptr->lock_root, iuse_flags_map, use_flags_map);

    // add installs to initial execution plan
    for (const auto &install: installs) {
        // if use flag, then skip
        if (install.find("use:") != std::string::npos) {
            continue;
        }
        ttrek_AddInstallToExecutionPlan(state_ptr, install, enhanced_requirements, global_use_flags_ht_ptr,
                                        iuse_flags_map, use_flags_map, execution_plan);
    }

    // check there is at least one direct install
    bool has_direct_install = false;
    for (const auto &install_spec: execution_plan) {
        if (install_spec.install_type == DIRECT_INSTALL) {
            has_direct_install = true;
            break;
        }
    }

    if (!has_direct_install) {
        execution_plan.clear();
        return;
    }

    int max_depth = 10;
    std::unordered_set<std::string> dependencies;
    std::unordered_set<std::string> reverse_dependencies;
    bool changed;
    do {
        changed = false;
        dependencies.clear();
        reverse_dependencies.clear();

        // compute dependencies and reverse dependencies of direct installs
        for (auto &install_spec: execution_plan) {
            if (install_spec.install_type == DIRECT_INSTALL) {

                if (reverse_dependencies_map.find(install_spec.package_name) !=
                    reverse_dependencies_map.end()) {

                    auto rdeps = reverse_dependencies_map.at(install_spec.package_name);
                    reverse_dependencies.insert(rdeps.begin(), rdeps.end());
                }

                if (dependencies_map.find(install_spec.package_name) != dependencies_map.end()) {
                    auto deps = dependencies_map.at(install_spec.package_name);
                    dependencies.insert(deps.begin(), deps.end());
                }

            } else if (install_spec.install_type == RDEP_INSTALL) {
                install_spec.install_type = UNKNOWN_INSTALL;
            }
        }

        // every UNKNOWN_INSTALL that is a reverse dependency of a direct install is a reverse dependency
        // and should be marked as RDEP_INSTALL
        bool has_unknown;
        do {
            has_unknown = false;
            for (auto &install_spec: execution_plan) {
                if (install_spec.install_type == UNKNOWN_INSTALL) {
                    if (reverse_dependencies.find(install_spec.package_name) != reverse_dependencies.end()) {
                        install_spec.install_type = RDEP_INSTALL;

                        if (reverse_dependencies_map.find(install_spec.package_name) !=
                            reverse_dependencies_map.end()) {

                            auto rdeps = reverse_dependencies_map.at(install_spec.package_name);
                            reverse_dependencies.insert(rdeps.begin(), rdeps.end());
                        }

                        if (dependencies_map.find(install_spec.package_name) != dependencies_map.end()) {
                            auto deps = dependencies_map.at(install_spec.package_name);
                            dependencies.insert(deps.begin(), deps.end());
                        }

                    } else if (dependencies.find(install_spec.package_name) != dependencies.end()) {
                        install_spec.install_type = DEP_INSTALL;

                        if (reverse_dependencies_map.find(install_spec.package_name) !=
                            reverse_dependencies_map.end()) {

                            auto rdeps = reverse_dependencies_map.at(install_spec.package_name);
                            reverse_dependencies.insert(rdeps.begin(), rdeps.end());
                        }

                        if (dependencies_map.find(install_spec.package_name) != dependencies_map.end()) {
                            auto deps = dependencies_map.at(install_spec.package_name);
                            dependencies.insert(deps.begin(), deps.end());
                        }

                    } else {
                        DBG(std::cout << "unknown install sofar: " << install_spec.package_name << std::endl);
                        if (enhanced_requirements.find(install_spec.package_name) != enhanced_requirements.end()) {
                            install_spec.install_type = ALREADY_INSTALLED;
                        } else {
                            has_unknown = true;
                        }
                    }
                }
            }
            max_depth--;
        } while (has_unknown && max_depth > 0);

        // set ALREADY_INSTALLED if the package is already installed and it is a DEP_INSTALL
        for (auto &install_spec: execution_plan) {
            if (install_spec.install_type == DEP_INSTALL && install_spec.exact_package_exists_in_lock_p &&
                install_spec.exact_use_flags_p) {
                install_spec.install_type = ALREADY_INSTALLED;
                reverse_dependencies_map.erase(install_spec.package_name);
                dependencies_map.erase(install_spec.package_name);
                changed = true;
            }
        }

    } while (changed && max_depth > 0);
}

static void
ttrek_PrintExecutionPlan(const std::vector<InstallSpec> &execution_plan) {
    for (const auto &install_spec: execution_plan) {
        if (install_spec.install_type == UNKNOWN_INSTALL) {
            DBG(std::cout << install_spec.package_name << "@" << install_spec.package_version << " (unknown install)"
                          << std::endl);
            continue;
        }

        if (install_spec.install_type == ALREADY_INSTALLED) {
            DBG(std::cout << install_spec.package_name << "@" << install_spec.package_version << " (already installed)"
                          << std::endl);
            continue;
        }

        std::cout << install_spec.package_name << "@" << install_spec.package_version;

        if (install_spec.install_type == RDEP_INSTALL) {
            std::cout << " (reverse dependency)";
        }

        if ((install_spec.install_type == DEP_INSTALL || install_spec.install_type == DIRECT_INSTALL) &&
            install_spec.exact_package_exists_in_lock_p && !install_spec.exact_use_flags_p) {
            std::cout << " (USE flags changed)";
        }

        std::cout << std::endl;
    }
}


static std::string ttrek_ReplaceAll(std::string inputStr, const std::string &from, const std::string &to) {
    size_t startPos = 0;
    std::string result;
    size_t fromLen = from.length();
    size_t pos;

    while ((pos = inputStr.find(from, startPos)) != std::string::npos) {
        result += inputStr.substr(startPos, pos - startPos) + to;
        startPos = pos + fromLen;
    }

    result += inputStr.substr(startPos);
    return result;
}

static const std::string &ttrek_RewriteUnsatMessage(const std::string &message) {
    // replace all use:* substrings with "use flag"
    // for example, use:threads 1.2.3 -> use flag +threads
    // for example, use:threads 0.0.0 -> use flag -threads
    // also skip the line after the occurrence of the use flag
    // except when it is a top level line (it does not start with "│")

    static std::string result;
    result.clear();

    std::istringstream iss(message);
    std::string line;
//    bool skip = false;
    while (std::getline(iss, line)) {
//        if (skip) {
//            skip = false;
//            continue;
//        }
        if (line.find("use:") != std::string::npos) {
//            skip = true;
            auto index = line.find("use:");
            auto index2 = line.find(' ', index);
            auto use_flag = line.substr(index + 4, index2 - index - 4);
            auto polarity = line.substr(index2 + 1, 5);
            auto rest = line.substr(index2 + 6); // version + two spaces
            // replace the word "installed" with the word "satisfied" in the rest of the line
            rest = ttrek_ReplaceAll(rest, "installed", "satisfied");
            // replace the word "versions" with the phrase "USE flags" in the rest of the line
            rest = ttrek_ReplaceAll(rest, "versions", "USE flags");
            result += line.substr(0, index);
            result += "USE flag ";
            if (polarity == "0.0.0") {
                result += "-";
            } else {
                result += "+";
            }
            result += use_flag;
            result += rest;
        } else {
            result += line;
        }
        result += "\n";
    }
    return result;
}

int
ttrek_InstallOrUpdate(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], ttrek_state_t *state_ptr, int *abort) {

    PackageDatabase db;
    db.set_strategy(state_ptr->strategy);

    std::map<std::string, std::unordered_set<std::string>> reverse_dependencies_map;
    ttrek_ParseReverseDependenciesFromLock(state_ptr->lock_root, reverse_dependencies_map);
    db.set_reverse_dependencies_map(reverse_dependencies_map);

    std::map<std::string, std::string> requirements;
    std::vector<std::string> installs;
    std::string message;

    if (TCL_OK != ttrek_Solve(interp, objc, objv, db, state_ptr, message, requirements, installs)) {
        return TCL_ERROR;
    }

    if (installs.empty()) {
        *abort = 1;
        std::cout << ttrek_RewriteUnsatMessage(message) << std::endl;
    } else {

        Tcl_Obj *use_flags_list_ptr = Tcl_NewListObj(0, NULL);
        Tcl_IncrRefCount(use_flags_list_ptr);
        if (TCL_OK != ttrek_GetUseFlags(interp, state_ptr->spec_root, use_flags_list_ptr)) {
            Tcl_DecrRefCount(use_flags_list_ptr);
            return TCL_ERROR;
        }

        Tcl_HashTable global_use_flags_ht;
        Tcl_InitHashTable(&global_use_flags_ht, TCL_STRING_KEYS);
        if (TCL_OK != ttrek_PopulateHashTableFromUseFlagsList(interp, use_flags_list_ptr, &global_use_flags_ht)) {
            Tcl_DecrRefCount(use_flags_list_ptr);
            Tcl_DeleteHashTable(&global_use_flags_ht);
            return TCL_ERROR;
        }

        // generate the execution plan
        std::vector<InstallSpec> execution_plan;
        ttrek_GenerateExecutionPlan(state_ptr, installs, requirements, db.get_dependencies_map(), &global_use_flags_ht,
                                    execution_plan);

        // print the execution plan

        if (execution_plan.empty()) {
            *abort = 1;
            std::cout << "Nothing to install!" << std::endl;
            Tcl_DecrRefCount(use_flags_list_ptr);
            Tcl_DeleteHashTable(&global_use_flags_ht);
            return TCL_OK;
        }

        std::cout << "The following packages will be installed:" << std::endl;
        ttrek_PrintExecutionPlan(execution_plan);

        if (!state_ptr->option_yes) {
            // get yes/no from user
            std::string answer;
            std::cout << "Do you want to proceed? [y/N] ";
            std::getline(std::cin, answer);
            if (answer != "y") {
                *abort = 1;
                Tcl_DecrRefCount(use_flags_list_ptr);
                Tcl_DeleteHashTable(&global_use_flags_ht);
                return TCL_OK;
            }
        }

        std::cout << std::endl;

        // ensure the directory skeleton exists
        if (TCL_OK != ttrek_EnsureSkeletonExists(interp, state_ptr)) {
            fprintf(stderr, "error: could not ensure directory skeleton exists\n");
            Tcl_DecrRefCount(use_flags_list_ptr);
            Tcl_DeleteHashTable(&global_use_flags_ht);
            return TCL_ERROR;
        }

        int package_num_total = 0;
        int package_num_current = 0;

        // Count the number of packages that need to be installed
        for (const auto &install_spec: execution_plan) {
            if (install_spec.install_type == ALREADY_INSTALLED) {
                continue;
            }
            package_num_total++;
        }

        struct utsname sysinfo;
        if (uname(&sysinfo)) {
            fprintf(stderr, "error: could not get system information\n");
            Tcl_DecrRefCount(use_flags_list_ptr);
            Tcl_DeleteHashTable(&global_use_flags_ht);
            return TCL_ERROR;
        }

        // perform the installation
        std::vector<InstallSpec> installs_from_lock_file_sofar;
        for (const auto &install_spec: execution_plan) {
            if (install_spec.install_type == ALREADY_INSTALLED) {
                continue;
            }
            auto package_name = install_spec.package_name;
            auto package_version = install_spec.package_version;
            auto direct_version_requirement = install_spec.direct_version_requirement;
            auto package_name_exists_in_lock_p = install_spec.package_name_exists_in_lock_p;

            // std::cout << "installing... " << package_name << "@" << package_version << std::endl;

            auto outcome = ttrek_InstallPackage(interp, state_ptr, &global_use_flags_ht, package_name.c_str(),
                                                package_version.c_str(), sysinfo.sysname, sysinfo.machine,
                                                direct_version_requirement.c_str(), package_name_exists_in_lock_p,
                                                ++package_num_current, package_num_total);

            ttrek_TelemetryPackageInstallEvent(package_name.c_str(), package_version.c_str(),
                                               sysinfo.sysname, sysinfo.machine, (outcome == TCL_OK ? 1 : 0),
                                               (install_spec.install_type == DIRECT_INSTALL ? 1 : 0));

            if (TCL_OK != outcome) {

                for (const auto &spec: installs_from_lock_file_sofar) {
                    if (package_name_exists_in_lock_p) {
                        fprintf(stderr, "restoring package files from old installation: %s\n",
                                spec.package_name.c_str());
                        if (TCL_OK != ttrek_RestoreTempFiles(interp, state_ptr, spec.package_name.c_str())) {
                            fprintf(stderr, "error: could not restore package files from old installation\n");
                        }
                    }
                }

                Tcl_DecrRefCount(use_flags_list_ptr);
                Tcl_DeleteHashTable(&global_use_flags_ht);
                return TCL_ERROR;

            }

            if (package_name_exists_in_lock_p) {
                installs_from_lock_file_sofar.push_back(install_spec);
            }
        }
        Tcl_DecrRefCount(use_flags_list_ptr);
        Tcl_DeleteHashTable(&global_use_flags_ht);

        if (TCL_OK != ttrek_UpdateSpecFileAfterInstall(interp, state_ptr)) {
            return TCL_ERROR;
        }

        if (TCL_OK != ttrek_UpdateLockFileAfterInstall(interp, state_ptr)) {
            return TCL_ERROR;
        }

        if (TCL_OK != ttrek_UpdateManifestFileAfterInstall(interp, state_ptr)) {
            fprintf(stderr, "error: could not update manifest file\n");
            return TCL_ERROR;
        }

        for (const auto &install_spec: installs_from_lock_file_sofar) {
            ttrek_DeleteTempFiles(interp, state_ptr, install_spec.package_name.c_str());
        }

    }

    return TCL_OK;
}

int ttrek_Uninstall(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], ttrek_state_t *state_ptr, int autoremove,
                    int *abort) {

    std::unordered_set<std::string> uninstalls;

    // prepare the initial list of packages to uninstall
    for (Tcl_Size i = 0; i < objc; i++) {
        std::string package_name = Tcl_GetString(objv[i]);
        uninstalls.insert(package_name);
    }

    // prepare the list of reverse dependencies to uninstall

    std::map<std::string, std::unordered_set<std::string>> reverse_dependencies_map;
    ttrek_ParseReverseDependenciesFromLock(state_ptr->lock_root, reverse_dependencies_map);

    bool changed;
    do {
        changed = false;
        for (const auto &uninstall: uninstalls) {
            auto it = reverse_dependencies_map.find(uninstall);
            if (it != reverse_dependencies_map.end()) {
                for (const auto &rdep: it->second) {
                    if (uninstalls.find(rdep) == uninstalls.end()) {
                        uninstalls.insert(rdep);
                        changed = true;
                    }
                }
            }
        }
    } while (changed);

    std::map<std::string, std::unordered_set<std::string>> dependencies_from_lock_map;
    ttrek_ParseDependenciesFromLock(state_ptr->lock_root, dependencies_from_lock_map);
    std::map<std::string, std::string> requirements;
    ttrek_ParseRequirementsFromSpecFile(state_ptr, requirements);

    do {
        changed = false;

        // remove all uninstalls from reverse_dependencies_map lists
        for (const auto &uninstall: uninstalls) {
            auto it = dependencies_from_lock_map.find(uninstall);
            if (it != dependencies_from_lock_map.end()) {
                for (const auto &dep: it->second) {
                    if (requirements.find(dep) != requirements.end()) {
                        DBG(std::cout << "cannot uninstall " << dep << " because it is a direct requirement"
                                      << std::endl);
                        continue;
                    }
                    if (reverse_dependencies_map.find(dep) != reverse_dependencies_map.end() &&
                        reverse_dependencies_map.at(dep).find(uninstall) != reverse_dependencies_map.at(dep).end()) {
                        reverse_dependencies_map.at(dep).erase(uninstall);
                        changed = true;
                        DBG(std::cout << "erased " << uninstall << " from " << dep << " rdeps" << std::endl);
                    }
                }
            }
        }

        // prepare the list of orphaned dependencies to uninstall
        if (autoremove) {
            for (const auto &uninstall: uninstalls) {
                auto it = dependencies_from_lock_map.find(uninstall);
                if (it != dependencies_from_lock_map.end()) {
                    for (const auto &dep: it->second) {
                        if (reverse_dependencies_map.find(dep) == reverse_dependencies_map.end() ||
                            reverse_dependencies_map.at(dep).empty()) {
                            if (uninstalls.find(dep) == uninstalls.end()) {
                                uninstalls.insert(dep);
                                changed = true;
                            }
                        }
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
            *abort = 1;
            return TCL_OK;
        }
    }

    for (const auto &uninstall: uninstalls) {
        if (TCL_OK != ttrek_UninstallPackage(interp, state_ptr, uninstall.c_str())) {
            return TCL_ERROR;
        }
    }

    // update spec, lock, and manifest files
    if (TCL_OK != ttrek_UpdateSpecFileAfterInstall(interp, state_ptr)) {
        fprintf(stderr, "error: could not update spec file\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_UpdateLockFileAfterInstall(interp, state_ptr)) {
        fprintf(stderr, "error: could not update lock file\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_UpdateManifestFileAfterInstall(interp, state_ptr)) {
        fprintf(stderr, "error: could not update manifest file\n");
        return TCL_ERROR;
    }

    return TCL_OK;
}
