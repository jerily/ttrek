/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <stdlib.h>
#include "subCmdDecls.h"
#include "ttrek_git.h"
#include "ttrek_scripts.h"

int ttrek_RunSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    int rc = TCL_OK;
    ttrek_state_t *state_ptr = NULL;
    const char **argv = NULL;
    Tcl_Obj *temp_file = NULL;

    int option_user = 0;
    int option_global = 0;
    const char *option_script = NULL;
    Tcl_ArgvInfo ArgTable[] = {
        { TCL_ARGV_CONSTANT, "-u",      INT2PTR(1), &option_user,   "run in user mode",            NULL },
        { TCL_ARGV_CONSTANT, "-g",      INT2PTR(1), &option_global, "run in global mode",          NULL },
        { TCL_ARGV_STRING,   "-script", NULL,       &option_script, "the named script to execute", NULL },
        { TCL_ARGV_END,      NULL,      NULL,       NULL,           NULL,                          NULL }
    };

    Tcl_Obj **remObjv;
    if (Tcl_ParseArgsObjv(interp, ArgTable, &objc, objv, &remObjv) != TCL_OK) {
        goto error;
    }

    if (option_user && option_global) {
        SetResult("conflicting options -u and -g");
        goto error;
    }

    int with_locking = 0;
    ttrek_mode_t mode = option_user ? MODE_USER : (option_global ? MODE_GLOBAL : MODE_LOCAL);
    state_ptr = ttrek_CreateState(interp, 0, 0, with_locking, mode, STRATEGY_LATEST);
    if (state_ptr == NULL) {
        SetResult("initializing ttrek state failed");
        goto error;
    }

    if (TCL_OK != ttrek_EnsureGitReady(interp, state_ptr)) {
        SetResult("resetting git repository failed");
        goto error;
    }

    // Let's construct the command to run

    if (option_script != NULL) {

        if (ttrek_ScriptsMakeTempFile(interp, state_ptr, option_script, &temp_file) != TCL_OK) {
            goto error;
        }

        if (temp_file == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("there is no named script \"%s\""
                " in the project", option_script));
            return TCL_ERROR;
        }

        Tcl_IncrRefCount(temp_file);

        if (ttrek_ScriptsDefineRunArgs(interp, temp_file, &objc, &argv) != TCL_OK) {
            goto error;
        }

    } else {

        // Skip the command "run"
        objc--;
        objv = remObjv + 1;

        DBG2(printf("objc: %" TCL_SIZE_MODIFIER "d", objc));
        if (objc < 1) {
            SetResult("no command to execute");
            goto error;
        }

        argv = ckalloc(sizeof(char *) * (objc + 1));
        for (Tcl_Size i = 0; i < objc; i++) {
            DBG2(printf("add arg #%" TCL_SIZE_MODIFIER "d: [%s]", i, Tcl_GetString(objv[i])));
            argv[i] = Tcl_GetString(objv[i]);
        }

    }

    argv[objc] = NULL;

    ttrek_EnvironmentStateSetVenv(state_ptr);

    rc = ttrek_ExecuteCommand(interp, objc, argv, NULL);

    goto done;

error:

    rc = TCL_ERROR;

done:

    ckfree(remObjv);
    if (state_ptr != NULL) {
        ttrek_DestroyState(state_ptr);
    }
    if (argv != NULL) {
        ckfree(argv);
    }
    if (temp_file != NULL) {
        Tcl_FSDeleteFile(temp_file);
        Tcl_DecrRefCount(temp_file);
    }
    return rc;

}
