/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "subCmdDecls.h"

int ttrek_InitSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    // write to file
    Tcl_Obj *filename_ptr = Tcl_NewStringObj(PACKAGES_JSON_FILE, -1);
    Tcl_IncrRefCount(filename_ptr);
    Tcl_Obj *cwd = Tcl_FSGetCwd(interp);
    if (!cwd) {
        fprintf(stderr, "error: getting current working directory failed\n");
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(cwd);
    Tcl_Obj *path_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, cwd, filename_ptr, &path_ptr)) {
        Tcl_DecrRefCount(cwd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(cwd);

    Tcl_IncrRefCount(path_ptr);
    if (TCL_OK == ttrek_CheckFileExists(path_ptr)) {
        fprintf(stderr, "error: %s already exists\n", PACKAGES_JSON_FILE);
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

    cJSON *root = cJSON_CreateObject();
    cJSON *name = cJSON_CreateString(project_name);
    cJSON_AddItemToObject(root, "name", name);
    cJSON *version = cJSON_CreateString(project_version);
    cJSON_AddItemToObject(root, "version", version);
    cJSON *scripts = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "scripts", scripts);
    cJSON *dependencies = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "dependencies", dependencies);
    cJSON *devDependencies = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "devDependencies", devDependencies);

    ttrek_WriteJsonFile(interp, path_ptr, root);

    ckfree(remObjv);
    return TCL_OK;
}
