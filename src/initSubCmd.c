/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "subCmdDecls.h"
#include "ttrek_git.h"
#include "ttrek_telemetry.h"

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
            {TCL_ARGV_CONSTANT, "-y", INT2PTR(1), &option_yes,   "Automatically answer yes to all the questions",                 NULL},
            {TCL_ARGV_CONSTANT, "-f", INT2PTR(1), &option_force, "Removes various protections against unfortunate side effects.", NULL},
            {TCL_ARGV_END,      NULL, NULL,       NULL,          NULL,                                                            NULL}
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
        if (fgets(project_name, sizeof(project_name), stdin) == NULL) {
            return TCL_ERROR;
        }
        fprintf(stdout, "version: ");
        if (fgets(project_version, sizeof(project_version), stdin) == NULL) {
            return TCL_ERROR;
        }

        // remove trailing newline
        project_name[strcspn(project_name, "\n")] = 0;
        project_version[strcspn(project_version, "\n")] = 0;
    }

    if (TCL_OK != ttrek_InitSpecFile(interp, path_to_spec_ptr, project_name, project_version)) {
        fprintf(stderr, "error: initializing spec file failed\n");
        Tcl_DecrRefCount(path_to_spec_ptr);
        Tcl_DecrRefCount(path_to_lock_ptr);
        Tcl_DecrRefCount(spec_file_name_ptr);
        Tcl_DecrRefCount(lock_file_name_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_InitLockFile(interp, path_to_lock_ptr)) {
        fprintf(stderr, "error: initializing lock file failed\n");
        Tcl_DecrRefCount(path_to_spec_ptr);
        Tcl_DecrRefCount(path_to_lock_ptr);
        Tcl_DecrRefCount(spec_file_name_ptr);
        Tcl_DecrRefCount(lock_file_name_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    Tcl_DecrRefCount(path_to_spec_ptr);
    Tcl_DecrRefCount(path_to_lock_ptr);
    Tcl_DecrRefCount(spec_file_name_ptr);
    Tcl_DecrRefCount(lock_file_name_ptr);
    ckfree(remObjv);

    ttrek_TelemetrySaveMachineId(interp);

    // ensure skeleton directories exist
    int with_locking = 0;
    ttrek_state_t *state_ptr = ttrek_CreateState(interp, option_yes, 0, with_locking, MODE_LOCAL, STRATEGY_LATEST);
    if (!state_ptr) {
        fprintf(stderr, "error: initializing ttrek state failed\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_EnsureSkeletonExists(interp, state_ptr)) {
        fprintf(stderr, "error: ensuring skeleton directories failed\n");
        ttrek_DestroyState(state_ptr);
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_GitInit(state_ptr)) {
        fprintf(stderr, "error: initializing git repository failed\n");
        ttrek_DestroyState(state_ptr);
        return TCL_ERROR;
    }

    ttrek_DestroyState(state_ptr);
    return TCL_OK;
}
