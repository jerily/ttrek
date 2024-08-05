/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "ttrek_buildInstructions.h"
#include "ttrek_useflags.h"
#include "ttrek_genInstall.h"

static int ttrek_GetBuildInstructions(Tcl_Interp *interp, cJSON *spec_root, cJSON **instructions_node_ptr) {

    *instructions_node_ptr = NULL;

    if (!cJSON_HasObjectItem(spec_root, "build")) {
        DBG2(printf("return NULL - no build property"));
        return TCL_OK;
    }

    cJSON *platforms_node = cJSON_GetObjectItem(spec_root, "build");

    if (!cJSON_HasObjectItem(platforms_node, "default")) {
        DBG2(printf("return NULL - no build->default property"));
        return TCL_OK;
    }

    *instructions_node_ptr = cJSON_GetObjectItem(platforms_node, "default");

    return TCL_OK;

}

int ttrek_RunBuildInstructions(Tcl_Interp *interp, ttrek_state_t *state_ptr) {

    cJSON *install_script_node = NULL;
    if (ttrek_GetBuildInstructions(interp, state_ptr->spec_root, &install_script_node) != TCL_OK) {
        return TCL_ERROR;
    }

    if (install_script_node == NULL) {
        DBG2(printf("return OK - no instructions"));
        return TCL_OK;
    }

    DBG2(printf("build instructions exist"));

    Tcl_Obj *use_flags_list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(use_flags_list_ptr);
    if (TCL_OK != ttrek_GetUseFlags(interp, state_ptr->spec_root, use_flags_list_ptr)) {
        Tcl_DecrRefCount(use_flags_list_ptr);
        return TCL_ERROR;
    }

    Tcl_HashTable use_flags_ht;
    Tcl_InitHashTable(&use_flags_ht, TCL_STRING_KEYS);
    if (TCL_OK != ttrek_PopulateHashTableFromUseFlagsList(interp, use_flags_list_ptr, &use_flags_ht)) {
        Tcl_DecrRefCount(use_flags_list_ptr);
        Tcl_DeleteHashTable(&use_flags_ht);
        return TCL_ERROR;
    }

    static const char *unknown_str = "unknown";

    const char *package_name = unknown_str;
    const char *package_version = unknown_str;

    cJSON *package_name_node = cJSON_GetObjectItem(state_ptr->spec_root, "name");
    if (package_name_node != NULL && cJSON_IsString(package_name_node)) {
        package_name = cJSON_GetStringValue(package_name_node);
    }

    cJSON *package_version_node = cJSON_GetObjectItem(state_ptr->spec_root, "version");
    if (package_version_node != NULL && cJSON_IsString(package_version_node)) {
        package_version = cJSON_GetStringValue(package_version_node);
    }

    Tcl_Obj *install_script_full = ttrek_generateInstallScript(interp, package_name,
        package_version, Tcl_GetString(state_ptr->project_build_dir_ptr),
        Tcl_GetString(state_ptr->project_install_dir_ptr), install_script_node,
        &use_flags_ht);

    Tcl_DecrRefCount(use_flags_list_ptr);
    Tcl_DeleteHashTable(&use_flags_ht);

    if (install_script_full == NULL) {
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(install_script_full);

    Tcl_Obj *path_to_install_file_ptr;
    ttrek_ResolvePath(interp, state_ptr->project_build_dir_ptr, Tcl_ObjPrintf("install-%s-%s.sh", package_name, package_version),
                      &path_to_install_file_ptr);
    ttrek_WriteChars(interp, path_to_install_file_ptr, install_script_full, 0744);

    Tcl_IncrRefCount(path_to_install_file_ptr);
    Tcl_DecrRefCount(install_script_full);

    static const char *static_string_one = "1";

    Tcl_Size argc = 3;
    const char *argv[4] = {
            Tcl_GetString(path_to_install_file_ptr),
            static_string_one,
            static_string_one,
            NULL
    };
    DBG(fprintf(stderr, "path_to_install_file: %s\n", Tcl_GetString(path_to_install_file_ptr)));

    if (ttrek_ExecuteCommand(interp, argc, argv, NULL) != TCL_OK) {
        fprintf(stderr, "error: could not execute install script to completion: %s\n",
                Tcl_GetString(path_to_install_file_ptr));
        Tcl_DecrRefCount(path_to_install_file_ptr);
        return TCL_ERROR;
    }

    Tcl_DecrRefCount(path_to_install_file_ptr);
    return TCL_OK;

}

