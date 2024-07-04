/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include "ttrek_resolvo.h"
#include "ttrek_git.h"

int ttrek_UpdateSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    int option_save_dev = 0;
    int option_user = 0;
    int option_global = 0;
    int option_yes = 0;
    int option_force = 0;
    const char *option_strategy = NULL;
    Tcl_ArgvInfo ArgTable[] = {
//            {TCL_ARGV_CONSTANT, "-save-dev", INT2PTR(1), &option_save_dev, "Save the package to the local repository as a dev dependency"},
            {TCL_ARGV_CONSTANT, "-y",        INT2PTR(1), &option_yes,      "answer yes to all questions",                                        NULL},
            {TCL_ARGV_CONSTANT, "-u",        INT2PTR(1), &option_user,     "update user directory tree",                                         NULL},
            {TCL_ARGV_CONSTANT, "-g",        INT2PTR(1), &option_global,   "update global directory tree",                                       NULL},
            {TCL_ARGV_CONSTANT, "-force",    INT2PTR(1), &option_force,    "force installation of already installed packages",                   NULL},
            {TCL_ARGV_STRING,   "-strategy", NULL,       &option_strategy, "strategy used for resolving dependencies (latest, favored, locked)", NULL},
            {TCL_ARGV_END, NULL,             NULL, NULL, NULL}
//            TCL_ARGV_AUTO_REST, TCL_ARGV_AUTO_HELP, TCL_ARGV_TABLE_END
    };

    Tcl_Obj **remObjv;
    Tcl_ParseArgsObjv(interp, ArgTable, &objc, objv, &remObjv);

    DBG(fprintf(stderr, "strategy: %s\n", option_strategy));

    if (option_user && option_global) {
        fprintf(stderr, "error: conflicting options -u and -g\n");
        ckfree(remObjv);
        return TCL_ERROR;
    }
    ttrek_mode_t mode = option_user ? MODE_USER : (option_global ? MODE_GLOBAL : MODE_LOCAL);
    ttrek_state_t *state_ptr = ttrek_CreateState(interp, option_yes, option_force, mode,
                                                 ttrek_StrategyFromString(option_strategy, STRATEGY_LATEST));
    if (!state_ptr) {
        fprintf(stderr, "error: initializing ttrek state failed\n");
        ckfree(remObjv);
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_EnsureGitReady(interp, state_ptr)) {
        fprintf(stderr, "error: ensuring git repository is ready failed\n");
        ttrek_DestroyState(state_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_TouchFile(interp, state_ptr->dirty_file_path_ptr)) {
        fprintf(stderr, "error: creating dirty directory failed\n");
        ttrek_DestroyState(state_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    Tcl_Size updateObjc = objc - 1;
    Tcl_Obj **updateObjv = NULL;

    Tcl_Obj *list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(list_ptr);
    if (updateObjc == 0) {
        // add direct dependencies from state_ptr->spec_root to updateObjv
        if (TCL_OK != ttrek_GetDirectDependencies(interp, state_ptr->spec_root, list_ptr)) {
            fprintf(stderr, "error: getting direct dependencies failed\n");
            Tcl_DecrRefCount(list_ptr);
            ttrek_DestroyState(state_ptr);
            ckfree(remObjv);
            return TCL_ERROR;
        }

        if (TCL_OK != Tcl_ListObjGetElements(interp, list_ptr, &updateObjc, &updateObjv)) {
            fprintf(stderr, "error: getting list elements failed\n");
            Tcl_DecrRefCount(list_ptr);
            ttrek_DestroyState(state_ptr);
            ckfree(remObjv);
            return TCL_ERROR;
        }

    } else {
        updateObjv = &remObjv[1];
    }

    int abort = 0;
    if (TCL_OK != ttrek_InstallOrUpdate(interp, updateObjc, updateObjv, state_ptr, &abort)) {
        ttrek_DestroyState(state_ptr);
        Tcl_DecrRefCount(list_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(list_ptr);
    ckfree(remObjv);

    if (!abort) {
        if (TCL_OK != ttrek_GitAmend(state_ptr)) {
            fprintf(stderr, "error: committing changes failed\n");
            ttrek_DestroyState(state_ptr);
            return TCL_ERROR;
        }
    }

    if (TCL_OK != Tcl_FSDeleteFile(state_ptr->dirty_file_path_ptr)) {
        fprintf(stderr, "error: removing dirty directory failed\n");
        ttrek_DestroyState(state_ptr);
        return TCL_ERROR;
    }

    ttrek_DestroyState(state_ptr);
    return TCL_OK;
}

