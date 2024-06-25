/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "installer.h"
#include "registry.h"
#include "base64.h"
#include "fsmonitor/fsmonitor.h"

#define MAX_INSTALL_SCRIPT_LEN 1048576
#define MAX_PATCH_FILE_SIZE 1048576


static void ttrek_AddPackageToSpec(cJSON *spec_root, const char *package_name,
                                   const char *version_requirement) {
    cJSON *dependencies = cJSON_GetObjectItem(spec_root, "dependencies");
    if (!dependencies) {
        dependencies = cJSON_CreateObject();
        cJSON_AddItemToObject(spec_root, "dependencies", dependencies);
    }

    cJSON *package = cJSON_GetObjectItem(dependencies, package_name);
    if (package) {
        cJSON_ReplaceItemInObject(dependencies, package_name, cJSON_CreateString(version_requirement));
    } else {
        cJSON_AddStringToObject(dependencies, package_name, version_requirement);
    }
}

static void ttrek_AddPackageToLock(cJSON *lock_root, const char *direct_version_requirement, const char *package_name, const char *package_version, cJSON *deps_node, Tcl_Obj *files_diff) {

    // add direct requirement to dependencies
    if (direct_version_requirement != NULL) {
        cJSON *deps = cJSON_GetObjectItem(lock_root, "dependencies");
        cJSON *dep = cJSON_GetObjectItem(deps, package_name);
        if (dep) {
            // modify the value
            cJSON_ReplaceItemInObject(deps, package_name, cJSON_CreateString(direct_version_requirement));
        } else {
            cJSON_AddStringToObject(deps, package_name, direct_version_requirement);
        }
    }

    // add the package to the packages list together with its dependencies

    cJSON *item_node = cJSON_CreateObject();
    cJSON_AddItemToObject(item_node, "version", cJSON_CreateString(package_version));
    cJSON *reqs_node = cJSON_CreateObject();

    for (int i = 0; i < cJSON_GetArraySize(deps_node); i++) {
        cJSON *dep_item = cJSON_GetArrayItem(deps_node, i);
        const char *dep_name = dep_item->string;
        const char *dep_version_requirement = dep_item->valuestring;
        fprintf(stderr, "dep_name: %s\n", dep_name);
        fprintf(stderr, "dep_version_requirement: %s\n", dep_version_requirement);
        cJSON_AddItemToObject(reqs_node, dep_name, cJSON_CreateString(dep_version_requirement));
    }
    cJSON_AddItemToObject(item_node, "requires", reqs_node);

    // add the files that were added to the package
    cJSON *files_node = cJSON_CreateArray();
    Tcl_Size files_diff_len;
    Tcl_ListObjLength(NULL, files_diff, &files_diff_len);
    for (int i = 0; i < files_diff_len; i++) {
        Tcl_Obj *file_diff_ptr;
        Tcl_ListObjIndex(NULL, files_diff, i, &file_diff_ptr);
        cJSON_AddItemToArray(files_node, cJSON_CreateString(Tcl_GetString(file_diff_ptr)));
    }
    cJSON_AddItemToObject(item_node, "files", files_node);

    cJSON *packages = cJSON_GetObjectItem(lock_root, "packages");
    if (!packages) {
        packages = cJSON_CreateObject();
        cJSON_AddItemToObject(lock_root, "packages", packages);
    }

    cJSON *package = cJSON_GetObjectItem(packages, package_name);
    if (package) {
        // modify the value
        cJSON_ReplaceItemInObject(packages, package_name, item_node);
    } else {
        cJSON_AddItemToObject(packages, package_name, item_node);
    }
}

static int ttrek_InstallScriptAndPatches(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *package_name,
                                         const char *package_version, const char *direct_version_requirement) {

    if (TCL_OK != ttrek_EnsureSkeletonExists(interp, state_ptr)) {
        fprintf(stderr, "error: could not ensure directory skeleton exists\n");
        return TCL_ERROR;
    }

    char install_spec_url[256];
    snprintf(install_spec_url, sizeof(install_spec_url), "%s/%s/%s", REGISTRY_URL, package_name, package_version);

    Tcl_DString ds;
    Tcl_DStringInit(&ds);
    if (TCL_OK != ttrek_RegistryGet(install_spec_url, &ds)) {
        fprintf(stderr, "error: could not get install spec for %s@%s\n", package_name, package_version);
        return TCL_ERROR;
    }

    cJSON *install_spec_root = cJSON_Parse(Tcl_DStringValue(&ds));
    cJSON *install_script_node = cJSON_GetObjectItem(install_spec_root, "install_script");
    if (!install_script_node) {
        fprintf(stderr, "error: install_script not found in spec file\n");
        cJSON_free(install_spec_root);
        return TCL_ERROR;
    }

    const char *base64_install_script_str = install_script_node->valuestring;

    cJSON *patches = cJSON_GetObjectItem(install_spec_root, "patches");
    if (patches) {
        for (int i = 0; i < cJSON_GetArraySize(patches); i++) {
            cJSON *patch_item = cJSON_GetArrayItem(patches, i);
            const char *patch_name = patch_item->string;
            const char *base64_patch_diff = patch_item->valuestring;
            fprintf(stderr, "patch_name: %s\n", patch_name);
//            fprintf(stderr, "patch_diff: %s\n", base64_patch_diff);

            char patch_diff[1024 * 1024];
            Tcl_Size patch_diff_len;
            base64_decode(base64_patch_diff, strnlen(base64_patch_diff, MAX_PATCH_FILE_SIZE), patch_diff, &patch_diff_len);

            char patch_filename[256];
            snprintf(patch_filename, sizeof(patch_filename), "patch-%s-%s-%s", package_name, package_version, patch_name);

            Tcl_Obj *patch_file_path_ptr;
            ttrek_ResolvePath(interp, state_ptr->project_build_dir_ptr, Tcl_NewStringObj(patch_filename, -1), &patch_file_path_ptr);
            ttrek_WriteChars(interp, patch_file_path_ptr, Tcl_NewStringObj(patch_diff, -1), 0666);
        }
    }
    char install_script[MAX_INSTALL_SCRIPT_LEN];
    Tcl_Size install_script_len;
    base64_decode(base64_install_script_str, strnlen(base64_install_script_str, MAX_INSTALL_SCRIPT_LEN), install_script,
                  &install_script_len);

    char install_filename[256];
    snprintf(install_filename, sizeof(install_filename), "install-%s-%s.sh", package_name, package_version);

    Tcl_Obj *path_to_install_file_ptr;
    ttrek_ResolvePath(interp, state_ptr->project_build_dir_ptr, Tcl_NewStringObj(install_filename, -1), &path_to_install_file_ptr);
    ttrek_WriteChars(interp, path_to_install_file_ptr, Tcl_NewStringObj(install_script, install_script_len), 0777);

    Tcl_Size argc = 2;
    const char *argv[3] = {Tcl_GetString(path_to_install_file_ptr), Tcl_GetString(state_ptr->project_venv_dir_ptr), NULL};
    fprintf(stderr, "path_to_install_file: %s\n", Tcl_GetString(path_to_install_file_ptr));

    ttrek_fsmonitor_state_t *fsmonitor_state_ptr = (ttrek_fsmonitor_state_t *) Tcl_Alloc(sizeof(ttrek_fsmonitor_state_t));
    fsmonitor_state_ptr->files_before = NULL;
    fsmonitor_state_ptr->files_diff = NULL;
    if (TCL_OK != ttrek_FSMonitor_AddWatch(interp, state_ptr->project_install_dir_ptr, fsmonitor_state_ptr)) {
        fprintf(stderr, "error: could not add watch on install directory\n");
        Tcl_Free((char *) fsmonitor_state_ptr);
        cJSON_free(install_spec_root);
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_ExecuteCommand(interp, argc, argv)) {
        fprintf(stderr, "error: could not execute install script to completion: %s\n",
                Tcl_GetString(path_to_install_file_ptr));
        (void) ttrek_FSMonitor_RemoveWatch(interp, fsmonitor_state_ptr);
        Tcl_Free((char *) fsmonitor_state_ptr);
        cJSON_free(install_spec_root);
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_FSMonitor_ReadChanges(interp, state_ptr->project_install_dir_ptr, fsmonitor_state_ptr)) {
        fprintf(stderr, "error: could not read changes from file system\n");
        (void) ttrek_FSMonitor_RemoveWatch(interp, fsmonitor_state_ptr);
        Tcl_Free((char *) fsmonitor_state_ptr);
        cJSON_free(install_spec_root);
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }

//    Tcl_Size files_diff_len;
//    Tcl_ListObjLength(interp, fsmonitor_state_ptr->files_diff, &files_diff_len);
//    for (int i = 0; i < files_diff_len; i++) {
//        Tcl_Obj *file_diff_ptr;
//        Tcl_ListObjIndex(interp, fsmonitor_state_ptr->files_diff, i, &file_diff_ptr);
//        fprintf(stderr, "file_diff: %s\n", Tcl_GetString(file_diff_ptr));
//    }

    cJSON *deps_node = cJSON_GetObjectItem(install_spec_root, "dependencies");
    if (direct_version_requirement != NULL) {
        if (strnlen(direct_version_requirement, 256) > 0) {
            ttrek_AddPackageToSpec(state_ptr->spec_root, package_name, direct_version_requirement);
            ttrek_AddPackageToLock(state_ptr->lock_root, direct_version_requirement, package_name, package_version, deps_node, fsmonitor_state_ptr->files_diff);
        } else {
            char package_version_with_caret_op[256];
            snprintf(package_version_with_caret_op, sizeof(package_version_with_caret_op), "^%s", package_version);
            ttrek_AddPackageToSpec(state_ptr->spec_root, package_name, package_version_with_caret_op);
            ttrek_AddPackageToLock(state_ptr->lock_root, package_version_with_caret_op, package_name, package_version, deps_node, fsmonitor_state_ptr->files_diff);
        }
    } else {
        ttrek_AddPackageToLock(state_ptr->lock_root, NULL, package_name, package_version, deps_node, fsmonitor_state_ptr->files_diff);
    }

    if (TCL_OK != ttrek_FSMonitor_RemoveWatch(interp, fsmonitor_state_ptr)) {
        fprintf(stderr, "error: could not remove watch on install directory\n");
        Tcl_Free((char *) fsmonitor_state_ptr);
        cJSON_free(install_spec_root);
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }

    Tcl_Free((char *) fsmonitor_state_ptr);
    cJSON_free(install_spec_root);
    Tcl_DStringFree(&ds);
    return TCL_OK;
}

static int ttrek_ExistsInLock(cJSON *lock_root, const char *package_name, const char *package_version, int *package_name_exists_in_lock_p) {

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

static int ttrek_EnsureDirectoryTreeExists(Tcl_Interp *interp, Tcl_Obj *file_path_ptr) {
    Tcl_Size len;
    Tcl_Obj *list_ptr = Tcl_FSSplitPath(file_path_ptr, &len);
    Tcl_Obj *dir_path_ptr = Tcl_NewObj();
    Tcl_IncrRefCount(dir_path_ptr);
    for (int i = 0; i < len - 1; i++) {
        Tcl_Obj *part_ptr;
        Tcl_ListObjIndex(interp, list_ptr, i, &part_ptr);
        Tcl_Obj *dir_path_ptr_copy;
        ttrek_ResolvePath(interp, dir_path_ptr, part_ptr, &dir_path_ptr_copy);
        if (ttrek_CheckFileExists(dir_path_ptr_copy) == TCL_OK) {
            dir_path_ptr = dir_path_ptr_copy;
            continue;
        }
        if (TCL_OK != Tcl_FSCreateDirectory(dir_path_ptr_copy)) {
            fprintf(stderr, "error: could not create directory %s\n", Tcl_GetString(dir_path_ptr_copy));
            Tcl_DecrRefCount(dir_path_ptr);
            Tcl_DecrRefCount(dir_path_ptr_copy);
            Tcl_DecrRefCount(list_ptr);
            return TCL_ERROR;
        }
        dir_path_ptr = dir_path_ptr_copy;
    }
    Tcl_DecrRefCount(list_ptr);
    Tcl_DecrRefCount(dir_path_ptr);
    return TCL_OK;
}

static int ttrek_BackupPackageFiles(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *package_name) {
    cJSON *packages = cJSON_GetObjectItem(state_ptr->lock_root, "packages");
    if (!packages) {
        return TCL_OK;
    }

    cJSON *package = cJSON_GetObjectItem(packages, package_name);
    if (!package) {
        return TCL_OK;
    }

    cJSON *files = cJSON_GetObjectItem(package, "files");
    if (!files) {
        return TCL_OK;
    }

    Tcl_Obj *temp_package_dir_ptr;
    ttrek_ResolvePath(interp, state_ptr->project_temp_dir_ptr, Tcl_NewStringObj(package_name, -1), &temp_package_dir_ptr);

    if (ttrek_CheckFileExists(temp_package_dir_ptr) == TCL_OK) {
        Tcl_Obj *error_ptr;
        if (TCL_OK != Tcl_FSRemoveDirectory(temp_package_dir_ptr, 1, &error_ptr)) {
            fprintf(stderr, "error: could not remove temp dir for package %s\n", package_name);
            Tcl_DecrRefCount(temp_package_dir_ptr);
            return TCL_ERROR;
        }
    }

    if (TCL_OK != Tcl_FSCreateDirectory(temp_package_dir_ptr)) {
        fprintf(stderr, "error: could not create temp dir for package %s\n", package_name);
        Tcl_DecrRefCount(temp_package_dir_ptr);
        return TCL_ERROR;
    }

    Tcl_Obj *file_path_ptr;
    Tcl_Obj *temp_file_path_ptr;
    for (int i = 0; i < cJSON_GetArraySize(files); i++) {
        cJSON *file = cJSON_GetArrayItem(files, i);
        const char *file_path = file->valuestring;

        // move the file from install dir to temp dir

        ttrek_ResolvePath(interp, state_ptr->project_install_dir_ptr, Tcl_NewStringObj(file_path, -1), &file_path_ptr);
        ttrek_ResolvePath(interp, temp_package_dir_ptr, Tcl_NewStringObj(file_path, -1), &temp_file_path_ptr);

        // create directory structure if it does not exist in temp_file_path_ptr

        if (TCL_OK != ttrek_EnsureDirectoryTreeExists(interp, temp_file_path_ptr)) {
            fprintf(stderr, "error: could not create directory tree for %s\n", file_path);
            Tcl_DecrRefCount(file_path_ptr);
            Tcl_DecrRefCount(temp_package_dir_ptr);
            Tcl_DecrRefCount(temp_file_path_ptr);
            return TCL_ERROR;
        }

        if (TCL_OK != Tcl_FSCopyFile(file_path_ptr, temp_file_path_ptr)) {
            fprintf(stderr, "error: could not move file %s to temp dir (%s)\n", Tcl_GetString(file_path_ptr), Tcl_GetString(temp_file_path_ptr));
            Tcl_DecrRefCount(file_path_ptr);
            Tcl_DecrRefCount(temp_package_dir_ptr);
            Tcl_DecrRefCount(temp_file_path_ptr);
            return TCL_ERROR;
        }
        Tcl_DecrRefCount(file_path_ptr);
        Tcl_DecrRefCount(temp_file_path_ptr);
    }

    return TCL_OK;
}

static int ttrek_DeletePackageFiles(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *package_name) {
    cJSON *packages = cJSON_GetObjectItem(state_ptr->lock_root, "packages");
    if (!packages) {
        return TCL_OK;
    }

    cJSON *package = cJSON_GetObjectItem(packages, package_name);
    if (!package) {
        return TCL_OK;
    }

    cJSON *files = cJSON_GetObjectItem(package, "files");
    if (!files) {
        return TCL_OK;
    }

    Tcl_Obj *file_path_ptr;
    for (int i = 0; i < cJSON_GetArraySize(files); i++) {
        cJSON *file = cJSON_GetArrayItem(files, i);
        const char *file_path = file->valuestring;
        ttrek_ResolvePath(interp, state_ptr->project_install_dir_ptr, Tcl_NewStringObj(file_path, -1), &file_path_ptr);
        if (TCL_OK != Tcl_FSDeleteFile(file_path_ptr)) {
            fprintf(stderr, "error: could not delete file %s\n", file_path);
            Tcl_DecrRefCount(file_path_ptr);
            return TCL_ERROR;
        }
        Tcl_DecrRefCount(file_path_ptr);
    }

    return TCL_OK;
}

static int ttrek_RestoreTempFiles(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *package_name) {
    cJSON *packages = cJSON_GetObjectItem(state_ptr->lock_root, "packages");
    if (!packages) {
        return TCL_OK;
    }

    cJSON *package = cJSON_GetObjectItem(packages, package_name);
    if (!package) {
        return TCL_OK;
    }

    cJSON *files = cJSON_GetObjectItem(package, "files");
    if (!files) {
        return TCL_OK;
    }

    for (int i = 0; i < cJSON_GetArraySize(files); i++) {
        cJSON *file = cJSON_GetArrayItem(files, i);
        const char *file_path = file->valuestring;
        // move the file from temp dir to install dir
        Tcl_Obj *temp_package_dir_ptr;
        ttrek_ResolvePath(interp, state_ptr->project_temp_dir_ptr, Tcl_NewStringObj(package_name, -1), &temp_package_dir_ptr);
        Tcl_Obj *temp_file_path_ptr;
        ttrek_ResolvePath(interp, temp_package_dir_ptr, Tcl_NewStringObj(file_path, -1), &temp_file_path_ptr);
        Tcl_Obj *file_path_ptr;
        ttrek_ResolvePath(interp, state_ptr->project_install_dir_ptr, Tcl_NewStringObj(file_path, -1), &file_path_ptr);
        if (TCL_OK != Tcl_FSRenameFile(temp_file_path_ptr, file_path_ptr)) {
            fprintf(stderr, "error: could not move file %s to temp dir\n", file_path);
            Tcl_DecrRefCount(temp_package_dir_ptr);
            Tcl_DecrRefCount(temp_file_path_ptr);
            Tcl_DecrRefCount(file_path_ptr);
            return TCL_ERROR;
        }
        Tcl_DecrRefCount(temp_package_dir_ptr);
        Tcl_DecrRefCount(temp_file_path_ptr);
        Tcl_DecrRefCount(file_path_ptr);
    }
    return TCL_OK;
}

static int ttrek_DeleteTempFiles(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *package_name) {
    Tcl_Obj *temp_package_dir_ptr;
    ttrek_ResolvePath(interp, state_ptr->project_temp_dir_ptr, Tcl_NewStringObj(package_name, -1), &temp_package_dir_ptr);
    Tcl_Obj *error_ptr;
    int result = Tcl_FSRemoveDirectory(temp_package_dir_ptr, 1, &error_ptr);
    Tcl_DecrRefCount(error_ptr);
    Tcl_DecrRefCount(temp_package_dir_ptr);
    return result;
}

int ttrek_InstallPackage(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *package_name, const char *package_version,
                         const char *direct_version_requirement) {

    int package_name_exists_in_lock_p;
    if (ttrek_ExistsInLock(state_ptr->lock_root, package_name, package_version, &package_name_exists_in_lock_p)) {
        fprintf(stderr, "info: %s@%s already installed\n", package_name, package_version);
        return TCL_OK;
    }

    if (package_name_exists_in_lock_p) {
        if (TCL_OK != ttrek_BackupPackageFiles(interp, state_ptr, package_name)) {
            fprintf(stderr, "error: could not backup package files from existing installation\n");
            return TCL_ERROR;
        }
        if (TCL_OK != ttrek_DeletePackageFiles(interp, state_ptr, package_name)) {
            fprintf(stderr, "error: could not delete package files from existing installation\n");
            return TCL_ERROR;
        }
    }

    if (TCL_OK !=
        ttrek_InstallScriptAndPatches(interp, state_ptr, package_name, package_version,
                                      direct_version_requirement)) {

        fprintf(stderr, "error: installing script & patches failed\n");

        if (package_name_exists_in_lock_p) {
            if (TCL_OK != ttrek_RestoreTempFiles(interp, state_ptr, package_name)) {
                fprintf(stderr, "error: could not restore package files from old installation\n");
            }
        }

        return TCL_ERROR;
    }

    if (package_name_exists_in_lock_p) {
        ttrek_DeleteTempFiles(interp, state_ptr, package_name);
    }

    return TCL_OK;
}