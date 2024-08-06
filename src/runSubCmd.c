/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <stdlib.h>
#include "subCmdDecls.h"
#include "ttrek_git.h"

int ttrek_RunSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    int option_user = 0;
    int option_global = 0;
    Tcl_ArgvInfo ArgTable[] = {
            {TCL_ARGV_CONSTANT, "-u",        INT2PTR(1), &option_user,     "run in user mode",                                          NULL},
            {TCL_ARGV_CONSTANT, "-g",        INT2PTR(1), &option_global,   "run in global mode",                                        NULL},
            {TCL_ARGV_END,      NULL,        NULL,       NULL,             NULL,                                                        NULL}
    };

    Tcl_Obj **remObjv;
    Tcl_ParseArgsObjv(interp, ArgTable, &objc, objv, &remObjv);

    if (option_user && option_global) {
        fprintf(stderr, "error: conflicting options -u and -g\n");
        ckfree(remObjv);
        return TCL_ERROR;
    }

    int with_locking = 0;
    ttrek_mode_t mode = option_user ? MODE_USER : (option_global ? MODE_GLOBAL : MODE_LOCAL);
    ttrek_state_t *state_ptr = ttrek_CreateState(interp, 0, 0, with_locking, mode, STRATEGY_LATEST);
    if (!state_ptr) {
        fprintf(stderr, "error: initializing ttrek state failed\n");
        ckfree(remObjv);
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_EnsureGitReady(interp, state_ptr)) {
        fprintf(stderr, "error: resetting git repository failed\n");
        ttrek_DestroyState(state_ptr);
        ckfree(remObjv);
        return TCL_ERROR;

    }

    DBG2(printf("objc: %" TCL_SIZE_MODIFIER "d", objc));
    if (objc < 2) {
        SetResult("no command to execute");
        ttrek_DestroyState(state_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    Tcl_Obj *filename_ptr = Tcl_NewStringObj("bin/", -1);
    Tcl_IncrRefCount(filename_ptr);
    Tcl_AppendObjToObj(filename_ptr, remObjv[1]);
    Tcl_Obj *path_to_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, state_ptr->project_install_dir_ptr, filename_ptr, &path_to_file_ptr)) {
        Tcl_DecrRefCount(filename_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(filename_ptr);
    Tcl_IncrRefCount(path_to_file_ptr);

    char ld_library_path_str[1024];
    snprintf(ld_library_path_str, 1024, "%s/lib", Tcl_GetString(state_ptr->project_install_dir_ptr));
    setenv("LD_LIBRARY_PATH", ld_library_path_str, 1);

    Tcl_Size argc = objc - 1;
    const char *argv[objc];
    argv[0] = Tcl_GetString(path_to_file_ptr);
    DBG2(printf("add arg #0: [%s]", Tcl_GetString(path_to_file_ptr)));
    Tcl_Size i;
    for (i = 2; i < objc; i++) {
        DBG2(printf("add arg #%" TCL_SIZE_MODIFIER "d: [%s]", i - 1, Tcl_GetString(remObjv[i])));
        argv[i - 1] = Tcl_GetString(remObjv[i]);
    }
    argv[i - 1] = NULL;
    if (ttrek_ExecuteCommand(interp, argc, argv, NULL) != TCL_OK) {
        ckfree(remObjv);
        return TCL_ERROR;
    }
    fprintf(stderr, "Exit status: OK\n");

    Tcl_DecrRefCount(path_to_file_ptr);
    ckfree(remObjv);
    return TCL_OK;
}
