/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "subCmdDecls.h"
#include "common.h"
#include "base64.h"
#include "registry.h"
#include "semver/semver.h"
#include "ttrek_resolvo.h"

#define MAX_INSTALL_SCRIPT_LEN 1048576

const char *ttrek_GetRangeOp(const char *package_range_str, const char **op) {
    if (package_range_str) {
        if (package_range_str[0] == '^') {
            *op = "^";
            package_range_str++;
        } else if (package_range_str[0] == '~') {
            *op = "~";
            package_range_str++;
        } else if (package_range_str[0] == '>') {
            if (package_range_str[1] == '=') {
                *op = ">=";
                package_range_str += 2;
            } else {
                *op = ">";
                package_range_str++;
            }
        } else if (package_range_str[0] == '<') {
            if (package_range_str[1] == '=') {
                *op = "<=";
                package_range_str += 2;
            } else {
                *op = "<";
                package_range_str++;
            }
        } else if (package_range_str[0] == '=') {
            *op = "=";
            package_range_str++;
        }
    }
    return package_range_str;
}

static int ttrek_AddPackageToSpec(Tcl_Interp *interp, cJSON *spec_root, const char *name, const char *version, const char *op) {
    cJSON *dependencies = cJSON_GetObjectItem(spec_root, "dependencies");
    Tcl_Obj *range_ptr = Tcl_NewStringObj(op, -1);
    Tcl_IncrRefCount(range_ptr);
    Tcl_AppendToObj(range_ptr, version, -1);
    const char *range_str = Tcl_GetString(range_ptr);
    cJSON *range_node = cJSON_CreateString(range_str);
    cJSON *pkg = cJSON_GetObjectItem(dependencies, name);
    if (pkg) {
        // modify the value
        cJSON_ReplaceItemInObject(dependencies, name, range_node);
    } else {
        cJSON_AddItemToObject(dependencies, name, range_node);
    }
    cJSON *devDependencies = cJSON_GetObjectItem(spec_root, "devDependencies");
    Tcl_DecrRefCount(range_ptr);
    return TCL_OK;
}

static int ttrek_EnsureLockFileExists(Tcl_Interp *interp, Tcl_Obj *path_ptr) {
    if (TCL_OK != ttrek_CheckFileExists(path_ptr)) {
        cJSON *root = cJSON_CreateObject();
        cJSON *deps = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "dependencies", deps);
        cJSON *packages = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "packages", packages);
        return ttrek_WriteJsonFile(interp, path_ptr, root);
    }
    return TCL_OK;
}

static int ttrek_AddPackageToLock(
        Tcl_Interp *interp,
        int direct_dependency_p,
        cJSON *lock_root,
        const char *name,
        const char *version,
        const char *op,
        Tcl_Obj *deps_list_ptr
) {

    cJSON *item_node = cJSON_CreateObject();
    cJSON_AddItemToObject(item_node, "version", cJSON_CreateString(version));
    cJSON *reqs_node = cJSON_CreateObject();

    Tcl_Size deps_length;
    if (TCL_OK != Tcl_ListObjLength(interp, deps_list_ptr, &deps_length)) {
        fprintf(stderr, "error: could not get length of deps list\n");
        return TCL_ERROR;
    }
    for (int i = 0; i < deps_length; i++) {
        Tcl_Obj *dep_list_ptr;
        Tcl_ListObjIndex(interp, deps_list_ptr, i, &dep_list_ptr);
        Tcl_Obj *dep_name_ptr;
        Tcl_Obj *dep_range_ptr;
        Tcl_ListObjIndex(interp, dep_list_ptr, 0, &dep_name_ptr);
        Tcl_ListObjIndex(interp, dep_list_ptr, 1, &dep_range_ptr);
        const char *dep_name = Tcl_GetString(dep_name_ptr);
        const char *dep_range = Tcl_GetString(dep_range_ptr);
        cJSON_AddItemToObject(reqs_node, dep_name, cJSON_CreateString(dep_range));
    }
    cJSON_AddItemToObject(item_node, "requires", reqs_node);

    // add range to dependencies
    if (direct_dependency_p) {
        cJSON *deps = cJSON_GetObjectItem(lock_root, "dependencies");
        cJSON *dep = cJSON_GetObjectItem(deps, name);
        Tcl_Obj *range_ptr = Tcl_NewStringObj(op, -1);
        Tcl_IncrRefCount(range_ptr);
        Tcl_AppendToObj(range_ptr, version, -1);
        const char *range_str = Tcl_GetString(range_ptr);
        cJSON *range_node = cJSON_CreateString(range_str);
        if (dep) {
            // modify the value
            cJSON_ReplaceItemInObject(deps, name, range_node);
        } else {
            cJSON_AddItemToObject(deps, name, range_node);
        }
        Tcl_DecrRefCount(range_ptr);
    }

    // add the package to the packages list together with its dependencies
    cJSON *packages = cJSON_GetObjectItem(lock_root, "packages");
    cJSON *pkg = cJSON_GetObjectItem(packages, name);
    if (pkg) {
        // modify the value
        cJSON_ReplaceItemInObject(packages, name, item_node);
    } else {
        cJSON_AddItemToObject(packages, name, item_node);
    }

    return TCL_OK;
}

static int ttrek_GetPackageVersionFromLock(Tcl_Interp *interp, cJSON *lock_root, const char *name, Tcl_Obj **installed_version) {

    cJSON *packages = cJSON_GetObjectItem(lock_root, "packages");
    if (!packages) {
        SetResult("error: packages node not found in lock file");
        return TCL_ERROR;
    }
    cJSON *pkg = cJSON_GetObjectItem(packages, name);
    if (pkg) {
        cJSON *version_node = cJSON_GetObjectItem(pkg, "version");
        *installed_version = Tcl_NewStringObj(cJSON_GetStringValue(version_node), -1);
        Tcl_IncrRefCount(*installed_version);
    } else {
        *installed_version = NULL;
    }
    return TCL_OK;
}

static int ttrek_SemverSatisfiesLock(Tcl_Interp *interp, cJSON *lock_root, const char *name, semver_t *semver_ptr) {

    // check that it satisfies dependencies
    cJSON *deps_node = cJSON_GetObjectItem(lock_root, "dependencies");
    cJSON *dep_node = cJSON_GetObjectItem(deps_node, name);
    if (dep_node) {
        const char *dep_str = dep_node->valuestring;
        const char *dep_op = "^";
        dep_str = ttrek_GetRangeOp(dep_str, &dep_op);
        semver_t dep_semver = {0, 0, 0, NULL, NULL};
        if (semver_parse(dep_str, &dep_semver)) {
            fprintf(stderr, "error: could not parse dep semver version: %s\n", dep_str);
            return TCL_ERROR;
        }
        if (!semver_satisfies(*semver_ptr, dep_semver, dep_op)) {
            return 0;
        }
    }

    // check that it satisfies requirements of other packages
    cJSON *packages = cJSON_GetObjectItem(lock_root, "packages");
    for (int i = 0; i < cJSON_GetArraySize(packages); i++) {
        cJSON *pkg = cJSON_GetArrayItem(packages, i);
        cJSON *reqs_node = cJSON_GetObjectItem(pkg, "requires");
        cJSON *req_node = cJSON_GetObjectItem(reqs_node, name);
        if (req_node) {
            const char *req_str = req_node->valuestring;
            const char *req_op = "^";
            req_str = ttrek_GetRangeOp(req_str, &req_op);
            semver_t req_semver = {0, 0, 0, NULL, NULL};
            if (semver_parse(req_str, &req_semver)) {
                fprintf(stderr, "error: could not parse req semver version: %s\n", req_str);
                return TCL_ERROR;
            }
            if (!semver_satisfies(*semver_ptr, req_semver, req_op)) {
                return 0;
            }
        }
    }
    return 1;
}

static int ttrek_AddPackageToPlan(Tcl_Interp *interp, const char *pkg_name, const char *resolved_version, cJSON *plan_root) {
    cJSON *pkg_node = cJSON_CreateObject();
    cJSON_AddItemToObject(pkg_node, "name", cJSON_CreateString(pkg_name));
    cJSON_AddItemToObject(pkg_node, "version", cJSON_CreateString(resolved_version));
    cJSON_AddItemToArray(plan_root, pkg_node);
    return TCL_OK;
}

static int
ttrek_InstallDependency(
        Tcl_Interp *interp,
        Tcl_Obj *path_to_rootdir,
        cJSON *spec_root,
        cJSON *lock_root,
        const char *pkg_name,
        semver_t *pkg_semver_ptr,
        const char *op,
        cJSON *plan_root
) {

    char package_versions_url[256];
    snprintf(package_versions_url, sizeof(package_versions_url), "%s/%s", REGISTRY_URL, pkg_name);
    Tcl_DString versions_ds;
    Tcl_DStringInit(&versions_ds);
    if (TCL_OK != ttrek_RegistryGet(package_versions_url, &versions_ds)) {
        fprintf(stderr, "error: could not get versions for %s\n", pkg_name);
        return TCL_ERROR;
    }

    // parse the versions json
    Tcl_Obj *resolved_version_ptr = NULL;
    cJSON *versions_root = cJSON_Parse(Tcl_DStringValue(&versions_ds));
    Tcl_DStringFree(&versions_ds);
    for (int i = 0; i < cJSON_GetArraySize(versions_root); i++) {
        cJSON *version_item = cJSON_GetArrayItem(versions_root, i);
        const char *version_str = version_item->valuestring;
        semver_t dep_semver = {0, 0, 0, NULL, NULL};
        if (semver_parse(version_str, &dep_semver)) {
            fprintf(stderr, "error: could not parse version: %s\n", version_str);
            cJSON_free(versions_root);
            return TCL_ERROR;
        }

        if (pkg_semver_ptr == NULL || semver_satisfies(dep_semver, *pkg_semver_ptr, op)) {
            if (ttrek_SemverSatisfiesLock(interp, lock_root, pkg_name, &dep_semver)) {
                fprintf(stderr, "info: found a version that satisfies the semver constraint: %s\n", version_str);
                resolved_version_ptr = Tcl_NewStringObj(version_str, -1);
                Tcl_IncrRefCount(resolved_version_ptr);
                break;
            }
        }

    }
    cJSON_free(versions_root);

    if (!resolved_version_ptr) {
        fprintf(stderr, "error: could not find a version that satisfies the semver constraint\n");
        return TCL_ERROR;
    }

    semver_t resolved_semver = {0, 0, 0, NULL, NULL};
    if (semver_parse(Tcl_GetString(resolved_version_ptr), &resolved_semver)) {
        fprintf(stderr, "error: could not parse resolved version: %s\n", Tcl_GetString(resolved_version_ptr));
        return TCL_ERROR;
    }
    Tcl_Obj *installed_version_ptr = NULL;
    if (TCL_OK ==
        ttrek_GetPackageVersionFromLock(interp, lock_root, pkg_name, &installed_version_ptr)) {
        if (installed_version_ptr) {
            const char *installed_version = Tcl_GetString(installed_version_ptr);
            semver_t installed_semver = {0, 0, 0, NULL, NULL};
            if (semver_parse(installed_version, &installed_semver)) {
                fprintf(stderr, "error: could not parse resolved version: %s\n", installed_version);
                return TCL_ERROR;
            }
            if (semver_satisfies(resolved_semver, installed_semver, "=")) {
                fprintf(stderr, "info: %s@%s is already installed\n", pkg_name, installed_version);
                Tcl_DecrRefCount(installed_version_ptr);
                return TCL_OK;
            }
            Tcl_DecrRefCount(installed_version_ptr);
        }
    }

    Tcl_Size resolved_version_len;
    const char *resolved_version = Tcl_GetStringFromObj(resolved_version_ptr, &resolved_version_len);
    char install_spec_url[256];
    snprintf(install_spec_url, sizeof(install_spec_url), "%s/%s/%s", REGISTRY_URL, pkg_name, resolved_version);

    Tcl_DString ds;
    Tcl_DStringInit(&ds);
    if (TCL_OK != ttrek_RegistryGet(install_spec_url, &ds)) {
        fprintf(stderr, "error: could not get install spec for %s@%s\n", pkg_name, resolved_version);
        return TCL_ERROR;
    }

    cJSON *install_spec_root = cJSON_Parse(Tcl_DStringValue(&ds));
    cJSON *install_script_node = cJSON_GetObjectItem(install_spec_root, "install_script");
    if (!install_script_node) {
        fprintf(stderr, "error: install_script not found in spec file\n");
        cJSON_free(install_spec_root);
        return TCL_ERROR;
    }
    Tcl_DStringFree(&ds);

    const char *base64_install_script_str = install_script_node->valuestring;

    fprintf(stderr, "install_script: %s\n", base64_install_script_str);

    cJSON *patches = cJSON_GetObjectItem(install_spec_root, "patches");
    if (patches) {
        for (int i = 0; i < cJSON_GetArraySize(patches); i++) {
            cJSON *patch_item = cJSON_GetArrayItem(patches, i);
            const char *patch_name = patch_item->string;
            const char *base64_patch_diff = patch_item->valuestring;
            fprintf(stderr, "patch_name: %s\n", patch_name);
//            fprintf(stderr, "patch_diff: %s\n", base64_patch_diff);

            char patch_diff[1024*1024];
            Tcl_Size patch_diff_len;
            base64_decode(base64_patch_diff, strnlen(base64_patch_diff, 1024*1024), patch_diff, &patch_diff_len);

            char patch_filename[256];
            snprintf(patch_filename, sizeof(patch_filename), "build/%s", patch_name);

            Tcl_Obj *patch_file_path_ptr;
            ttrek_ResolvePath(interp, path_to_rootdir, Tcl_NewStringObj(patch_filename, -1), &patch_file_path_ptr);
            ttrek_WriteChars(interp, patch_file_path_ptr, Tcl_NewStringObj(patch_diff, -1), 0666);

        }
    }

    cJSON *version_node = cJSON_GetObjectItem(install_spec_root, "version");
    if (strncmp(resolved_version, version_node->valuestring, resolved_version_len)) {
        fprintf(stderr, "error: resolved version does not match version in spec file\n");
        cJSON_free(install_spec_root);
        return TCL_ERROR;
    }

    fprintf(stderr, "resolved_version: %s\n", resolved_version);

    char install_script[MAX_INSTALL_SCRIPT_LEN];
    Tcl_Size install_script_len;
    base64_decode(base64_install_script_str, strnlen(base64_install_script_str, MAX_INSTALL_SCRIPT_LEN), install_script, &install_script_len);

    char install_filename[256];
    snprintf(install_filename, sizeof(install_filename), "build/install-%s-%s.sh", pkg_name, resolved_version);

    Tcl_Obj *install_file_path_ptr;
    ttrek_ResolvePath(interp, path_to_rootdir, Tcl_NewStringObj(install_filename, -1), &install_file_path_ptr);
    ttrek_WriteChars(interp, install_file_path_ptr, Tcl_NewStringObj(install_script, install_script_len), 0777);

    int deps_length = 0;
    Tcl_Obj *deps_list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(deps_list_ptr);
    cJSON *deps_node = cJSON_GetObjectItem(install_spec_root, "dependencies");
    for (int i = 0; i < cJSON_GetArraySize(deps_node); i++) {
        cJSON *dep_item = cJSON_GetArrayItem(deps_node, i);
        const char *dep_name = dep_item->string;
        const char *dep_range = dep_item->valuestring;
        fprintf(stderr, "dep_name: %s\n", dep_name);
        fprintf(stderr, "dep_range: %s\n", dep_range);
        // add to list of dependencies
        const char *dep_op = "^";
        dep_range = ttrek_GetRangeOp(dep_range, &dep_op);
        Tcl_Obj *dep_range_ptr = Tcl_NewStringObj(dep_op, -1);
        Tcl_AppendToObj(dep_range_ptr, dep_range, -1);
        Tcl_Obj *objv[2] = {Tcl_NewStringObj(dep_name, -1), dep_range_ptr};
        Tcl_Obj *dep_list_ptr = Tcl_NewListObj(2, objv);
        Tcl_ListObjAppendElement(interp, deps_list_ptr, dep_list_ptr);
        deps_length++;
    }
    cJSON_free(install_spec_root);


    for (int i = 0; i < deps_length; i++) {
        Tcl_Obj *dep_list_ptr;
        Tcl_ListObjIndex(interp, deps_list_ptr, i, &dep_list_ptr);
        Tcl_Obj *dep_name_ptr;
        Tcl_Obj *dep_range_ptr;
        Tcl_ListObjIndex(interp, dep_list_ptr, 0, &dep_name_ptr);
        Tcl_ListObjIndex(interp, dep_list_ptr, 1, &dep_range_ptr);
        const char *dep_name = Tcl_GetString(dep_name_ptr);
        const char *dep_range = Tcl_GetString(dep_range_ptr);

        const char *dep_op = "=";
        const char *dep_version = ttrek_GetRangeOp(dep_range, &dep_op);
        semver_t dep_semver = {0, 0, 0, NULL, NULL};
        if (semver_parse(dep_version, &dep_semver)) {
            fprintf(stderr, "error: could not parse dep range version: %s\n", dep_range);
            return TCL_ERROR;
        }
        if (TCL_OK !=
            ttrek_InstallDependency(interp, path_to_rootdir, NULL, lock_root, dep_name, &dep_semver, dep_op, plan_root)) {
            fprintf(stderr, "error: could not install dependency: %s@%s\n", dep_name, dep_range);
            return TCL_ERROR;
        }
    }

    Tcl_Obj *path_to_install_file_ptr;
    ttrek_ResolvePath(interp, path_to_rootdir, Tcl_NewStringObj(install_filename, -1), &path_to_install_file_ptr);

    Tcl_Size argc = 2;
    const char *argv[3] = {Tcl_GetString(path_to_install_file_ptr), Tcl_GetString(path_to_rootdir), NULL };
    fprintf(stderr, "path_to_install_file: %s\n", Tcl_GetString(path_to_install_file_ptr));

    if (TCL_OK != ttrek_AddPackageToPlan(interp, pkg_name, resolved_version, plan_root)) {
        fprintf(stderr, "error: could not add package to plan\n");
        return TCL_ERROR;
    }

    if (0 && TCL_OK != ttrek_ExecuteCommand(interp, argc, argv)) {
        fprintf(stderr, "error: could not execute install script to completion: %s\n", Tcl_GetString(path_to_install_file_ptr));
        return TCL_ERROR;
    }
    fprintf(stderr, "interp result: %s\n", Tcl_GetString(Tcl_GetObjResult(interp)));

    int direct_dependency_p = spec_root != NULL;
    if (direct_dependency_p) {
        if (pkg_semver_ptr) {
            char rendered_pkg_semver[256];
            semver_render(pkg_semver_ptr, rendered_pkg_semver);
            Tcl_Obj *rendered = Tcl_NewStringObj(rendered_pkg_semver, -1);
            ttrek_AddPackageToSpec(interp, spec_root, pkg_name, Tcl_GetString(rendered), op);
        } else {
            ttrek_AddPackageToSpec(interp, spec_root, pkg_name, resolved_version, op);
        }
    }
    ttrek_AddPackageToLock(interp, direct_dependency_p, lock_root, pkg_name, resolved_version, op, deps_list_ptr);

    Tcl_DecrRefCount(deps_list_ptr);
    Tcl_DecrRefCount(resolved_version_ptr);
    return TCL_OK;
}

int ttrek_InstallSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *project_home_dir_ptr = ttrek_GetProjectHomeDir(interp);
    if (!project_home_dir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return TCL_ERROR;
    }

    fprintf(stderr, "project_home_dir: %s\n", Tcl_GetString(project_home_dir_ptr));

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

    if (TCL_OK != ttrek_CheckFileExists(path_to_spec_file_ptr)) {
        fprintf(stderr, "error: %s does not exist, run 'ttrek init' first\n", SPEC_JSON_FILE);
        Tcl_DecrRefCount(project_home_dir_ptr);
        Tcl_DecrRefCount(path_to_spec_file_ptr);
        return TCL_ERROR;
    }

    int option_save_dev = 0;
    int option_user = 0;
    int option_global = 0;
    Tcl_ArgvInfo ArgTable[] = {
//            {TCL_ARGV_CONSTANT, "-save-dev", INT2PTR(1), &option_save_dev, "Save the package to the local repository as a dev dependency"},
            {TCL_ARGV_CONSTANT, "-u", INT2PTR(1), &option_user, "install as a user package"},
            {TCL_ARGV_CONSTANT, "-g", INT2PTR(1), &option_global, "install as a global package"},
            {TCL_ARGV_END, NULL, NULL, NULL, NULL}
    };

    Tcl_Obj **remObjv;
    Tcl_ParseArgsObjv(interp, ArgTable, &objc, objv, &remObjv);

    if (TCL_OK != ttrek_install(interp, objc, remObjv)) {
        Tcl_DecrRefCount(project_home_dir_ptr);
        Tcl_DecrRefCount(path_to_spec_file_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    Tcl_DecrRefCount(project_home_dir_ptr);
    Tcl_DecrRefCount(path_to_spec_file_ptr);
    ckfree(remObjv);

    return TCL_OK;
}
