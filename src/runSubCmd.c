/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <stdlib.h>
#include "subCmdDecls.h"
#include "ttrek_git.h"

int ttrek_RunSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    ttrek_state_t *state_ptr = ttrek_CreateState(interp, 1, 1, MODE_LOCAL, STRATEGY_LATEST);
    if (!state_ptr) {
        fprintf(stderr, "error: initializing ttrek state failed\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_GitResetHard(state_ptr)) {
        fprintf(stderr, "error: resetting git repository failed\n");
        ttrek_DestroyState(state_ptr);
//        ckfree(remObjv);
        return TCL_ERROR;

    }

    Tcl_Obj *filename_ptr = Tcl_NewStringObj("bin/", -1);
    Tcl_IncrRefCount(filename_ptr);
    Tcl_AppendObjToObj(filename_ptr, objv[0]);
    Tcl_Obj *path_to_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, state_ptr->project_install_dir_ptr, filename_ptr, &path_to_file_ptr)) {
        Tcl_DecrRefCount(filename_ptr);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(filename_ptr);
    Tcl_IncrRefCount(path_to_file_ptr);

    char ld_library_path_str[1024];
    snprintf(ld_library_path_str, 1024, "%s/lib", Tcl_GetString(state_ptr->project_install_dir_ptr));

    Tcl_Size argc = objc;
    const char *argv[objc];
    setenv("LD_LIBRARY_PATH", ld_library_path_str, 1);
    argv[0] = Tcl_GetString(path_to_file_ptr);
    fprintf(stderr, "path_to_file: %s\n", Tcl_GetString(path_to_file_ptr));
    for (Tcl_Size i = 1; i < objc; i++) {
        argv[i] = Tcl_GetString(objv[i]);
    }
    if (TCL_OK != ttrek_ExecuteCommand(interp, argc, argv)) {
        return TCL_ERROR;
    }
    fprintf(stderr, "interp result: %s\n", Tcl_GetString(Tcl_GetObjResult(interp)));

    Tcl_DecrRefCount(path_to_file_ptr);

    return TCL_OK;
}
