/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "subCmdDecls.h"

int ttrek_InitSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    Tcl_Obj *spec_file_name_ptr = Tcl_NewStringObj(SPEC_JSON_FILE, -1);
    Tcl_IncrRefCount(spec_file_name_ptr);
    Tcl_Obj *lock_file_name_ptr = Tcl_NewStringObj(LOCK_JSON_FILE, -1);
    Tcl_IncrRefCount(lock_file_name_ptr);
    Tcl_Obj *cwd = Tcl_FSGetCwd(interp);
    if (!cwd) {
        fprintf(stderr, "error: getting current working directory failed\n");
        Tcl_DecrRefCount(spec_file_name_ptr);
        Tcl_DecrRefCount(lock_file_name_ptr);
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(cwd);
    Tcl_Obj *path_to_spec_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, cwd, spec_file_name_ptr, &path_to_spec_ptr)) {
        Tcl_DecrRefCount(cwd);
        Tcl_DecrRefCount(spec_file_name_ptr);
        Tcl_DecrRefCount(lock_file_name_ptr);
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(path_to_spec_ptr);

    Tcl_Obj *path_to_lock_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, cwd, lock_file_name_ptr, &path_to_lock_ptr)) {
        Tcl_DecrRefCount(cwd);
        Tcl_DecrRefCount(spec_file_name_ptr);
        Tcl_DecrRefCount(lock_file_name_ptr);
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(path_to_lock_ptr);
    Tcl_DecrRefCount(cwd);

    if (TCL_OK == ttrek_CheckFileExists(path_to_spec_ptr)) {
        fprintf(stderr, "error: %s already exists\n", SPEC_JSON_FILE);
        Tcl_DecrRefCount(path_to_spec_ptr);
        Tcl_DecrRefCount(path_to_lock_ptr);
        Tcl_DecrRefCount(spec_file_name_ptr);
        Tcl_DecrRefCount(lock_file_name_ptr);
        return TCL_ERROR;
    }

    Tcl_Obj *project_venv_dir_ptr = ttrek_GetProjectVenvDir(interp, cwd);
    if (TCL_OK == ttrek_CheckFileExists(project_venv_dir_ptr)) {
        fprintf(stderr, "error: %s already exists\n", Tcl_GetString(project_venv_dir_ptr));
        Tcl_DecrRefCount(path_to_spec_ptr);
        Tcl_DecrRefCount(path_to_lock_ptr);
        Tcl_DecrRefCount(spec_file_name_ptr);
        Tcl_DecrRefCount(lock_file_name_ptr);
        Tcl_DecrRefCount(project_venv_dir_ptr);
        return TCL_ERROR;
    }


    int option_yes = 0;
    int option_force = 0;
    Tcl_ArgvInfo ArgTable[] = {
            {TCL_ARGV_CONSTANT, "-y", INT2PTR(1), &option_yes, "Automatically answer yes to all the questions"},
            {TCL_ARGV_CONSTANT, "-f", INT2PTR(1), &option_force, "Removes various protections against unfortunate side effects."},
            {TCL_ARGV_END,      NULL, NULL,               NULL,             NULL}
    };

    Tcl_Obj **remObjv;
    Tcl_ParseArgsObjv(interp, ArgTable, &objc, objv, &remObjv);

//    fprintf(stderr, "option_yes: %d\n", option_yes);
//    fprintf(stderr, "option_force: %d\n", option_force);

    char project_name[256] = "example";
    char project_version[256] = "1.0.0";
    if (!option_yes) {
        // read "name" and "version" from stdin
        fprintf(stdout, "name: ");
        fgets(project_name, sizeof(project_name), stdin);
        fprintf(stdout, "version: ");
        fgets(project_version, sizeof(project_version), stdin);

        // remove trailing newline
        project_name[strcspn(project_name, "\n")] = 0;
        project_version[strcspn(project_version, "\n")] = 0;
    }

    cJSON *spec_root = cJSON_CreateObject();
    cJSON *name = cJSON_CreateString(project_name);
    cJSON_AddItemToObject(spec_root, "name", name);
    cJSON *version = cJSON_CreateString(project_version);
    cJSON_AddItemToObject(spec_root, "version", version);
    cJSON *scripts = cJSON_CreateObject();
    cJSON_AddItemToObject(spec_root, "scripts", scripts);
    cJSON *spec_dependencies = cJSON_CreateObject();
    cJSON_AddItemToObject(spec_root, "dependencies", spec_dependencies);
    cJSON *devDependencies = cJSON_CreateObject();
    cJSON_AddItemToObject(spec_root, "devDependencies", devDependencies);

    ttrek_WriteJsonFile(interp, path_to_spec_ptr, spec_root);

    cJSON *lock_root = cJSON_CreateObject();
    cJSON *lock_packages = cJSON_CreateObject();
    cJSON_AddItemToObject(lock_root, "packages", lock_packages);
    cJSON *lock_dependencies = cJSON_CreateObject();
    cJSON_AddItemToObject(lock_root, "dependencies", lock_dependencies);
    ttrek_WriteJsonFile(interp, path_to_lock_ptr, lock_root);

    cJSON_Delete(spec_root);
    cJSON_Delete(lock_root);
    Tcl_DecrRefCount(path_to_spec_ptr);
    Tcl_DecrRefCount(path_to_lock_ptr);
    Tcl_DecrRefCount(spec_file_name_ptr);
    Tcl_DecrRefCount(lock_file_name_ptr);
    ckfree(remObjv);
    return TCL_OK;
}
