/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "installer.h"
#include "registry.h"
#include "base64.h"

#define MAX_INSTALL_SCRIPT_LEN 1048576

static int ttrek_InstallScriptAndPatches(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr, const char *package_name, const char *package_version) {
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

            char patch_diff[1024*1024];
            Tcl_Size patch_diff_len;
            base64_decode(base64_patch_diff, strnlen(base64_patch_diff, 1024*1024), patch_diff, &patch_diff_len);

            char patch_filename[256];
            snprintf(patch_filename, sizeof(patch_filename), "build/%s", patch_name);

            Tcl_Obj *patch_file_path_ptr;
            ttrek_ResolvePath(interp, project_home_dir_ptr, Tcl_NewStringObj(patch_filename, -1), &patch_file_path_ptr);
            ttrek_WriteChars(interp, patch_file_path_ptr, Tcl_NewStringObj(patch_diff, -1), 0666);
        }
    }
    char install_script[MAX_INSTALL_SCRIPT_LEN];
    Tcl_Size install_script_len;
    base64_decode(base64_install_script_str, strnlen(base64_install_script_str, MAX_INSTALL_SCRIPT_LEN), install_script, &install_script_len);

    char install_filename[256];
    snprintf(install_filename, sizeof(install_filename), "build/install-%s-%s.sh", package_name, package_version);

    Tcl_Obj *install_file_path_ptr;
    ttrek_ResolvePath(interp, project_home_dir_ptr, Tcl_NewStringObj(install_filename, -1), &install_file_path_ptr);
    ttrek_WriteChars(interp, install_file_path_ptr, Tcl_NewStringObj(install_script, install_script_len), 0777);

    Tcl_Obj *path_to_install_file_ptr;
    ttrek_ResolvePath(interp, project_home_dir_ptr, Tcl_NewStringObj(install_filename, -1), &path_to_install_file_ptr);

    Tcl_Size argc = 2;
    const char *argv[3] = {Tcl_GetString(path_to_install_file_ptr), Tcl_GetString(project_home_dir_ptr), NULL };
    fprintf(stderr, "path_to_install_file: %s\n", Tcl_GetString(path_to_install_file_ptr));

    if (TCL_OK != ttrek_ExecuteCommand(interp, argc, argv)) {
        fprintf(stderr, "error: could not execute install script to completion: %s\n", Tcl_GetString(path_to_install_file_ptr));
        return TCL_ERROR;
    }

    cJSON_free(install_spec_root);
    Tcl_DStringFree(&ds);
    return TCL_OK;
}

int ttrek_InstallPackage(Tcl_Interp *interp, const char *package_name, const char *package_version) {

    Tcl_Obj *project_home_dir_ptr = ttrek_GetProjectHomeDir(interp);
    if (!project_home_dir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_InstallScriptAndPatches(interp, project_home_dir_ptr, package_name, package_version)) {
        fprintf(stderr, "error: installing script & patches failed\n");
        return TCL_ERROR;
    }

    return TCL_OK;
}