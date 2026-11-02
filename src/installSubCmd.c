/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "subCmdDecls.h"
#include "common.h"
#include "ttrek_solver.h"
#include "ttrek_git.h"
#include "ttrek_buildInstructions.h"
#include "ttrek_scripts.h"
#include "ttrek_genInstall.h"
#include "registry.h"
#include "ttrek_telemetry.h"

#define MAX_INSTALL_SCRIPT_LEN 1048576

static int ttrek_InstallRunHook(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *hook_str) {

    int rc = TCL_OK;
    const char **argv = NULL;
    Tcl_Size argc;
    Tcl_Obj *temp_file = NULL;

    DBG2(fprintf(stderr, "want to run hook [%s]", hook_str));

    if (ttrek_ScriptsMakeTempFile(interp, state_ptr, hook_str, &temp_file) != TCL_OK) {
        goto error;
    }

    if (temp_file == NULL) {
        DBG2(fprintf(stderr, "no hook [%s] defined", hook_str));
        goto done;
    }

    Tcl_IncrRefCount(temp_file);

    if (ttrek_ScriptsDefineRunArgs(interp, temp_file, &argc, &argv) != TCL_OK) {
        goto error;
    }

    ttrek_EnvironmentStateSetVenv(state_ptr);
    rc = ttrek_ExecuteCommand(interp, argc, argv, NULL);
    ttrek_EnvironmentStateRestore();

    goto done;

error:

    rc = TCL_ERROR;

done:

    if (temp_file != NULL) {
        Tcl_FSDeleteFile(temp_file);
        Tcl_DecrRefCount(temp_file);
    }
    if (argv != NULL) {
        ckfree(argv);
    }
    return rc;

}

int ttrek_InstallSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

//    int option_save_dev = 0;
    int option_yes = 0;
    int option_force = 0;
    int option_mode = MODE_LOCAL;
    int option_fail_verbose = 0;

    const char *option_strategy = NULL;
    const char *registry = NULL;
    Tcl_ArgvInfo ArgTable[] = {
            {TCL_ARGV_STRING, "-registry", NULL, &registry, "local registry directory", NULL},
//            {TCL_ARGV_CONSTANT, "-save-dev", INT2PTR(1), &option_save_dev, "Save the package to the local repository as a dev dependency"},
            {TCL_ARGV_CONSTANT, "-fail-verbose", INT2PTR(1),              &option_fail_verbose, "show verbose output on failure",                                     NULL},
            {TCL_ARGV_CONSTANT, "-y",            INT2PTR(1),              &option_yes,          "answer yes to all questions",                                        NULL},
            {TCL_ARGV_CONSTANT, "-u",            INT2PTR(MODE_USER),      &option_mode,         "install as a user package",                                          NULL},
            {TCL_ARGV_CONSTANT, "-g",            INT2PTR(MODE_GLOBAL),    &option_mode,         "install as a global package",                                        NULL},
            {TCL_ARGV_CONSTANT, "-force",        INT2PTR(1),              &option_force,        "force installation of already installed packages",                   NULL},
            {TCL_ARGV_STRING,   "-strategy",     NULL,                    &option_strategy,     "strategy used for resolving dependencies (latest, favored, locked)", NULL},
            {TCL_ARGV_END,      NULL,            NULL,                     NULL,            NULL,                                                                 NULL}
//            TCL_ARGV_AUTO_REST, TCL_ARGV_AUTO_HELP, TCL_ARGV_TABLE_END
    };

    Tcl_Obj **remObjv = NULL;
    if (Tcl_ParseArgsObjv(interp, ArgTable, &objc, objv, &remObjv) != TCL_OK) {
        return TCL_ERROR;
    }
    for (Tcl_Size i = 1; i < objc; ++i) {
        if (Tcl_GetString(remObjv[i])[0] == '-') {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown install option: %s", Tcl_GetString(remObjv[i])));
            ckfree(remObjv);
            return TCL_ERROR;
        }
    }

    if (ttrek_RegistrySetDirectory(interp, registry) != TCL_OK) {
        ckfree(remObjv);
        return TCL_ERROR;
    }
    ttrek_state_t *state_ptr = ttrek_CreateState(interp, option_yes, option_force, 1, (ttrek_mode_t)option_mode,
                                                 ttrek_StrategyFromString(option_strategy, STRATEGY_FAVORED));
    if (!state_ptr) {
        fprintf(stderr, "error: initializing ttrek state failed\n");
        ckfree(remObjv);
        return TCL_ERROR;
    }

    if (ttrek_RegistryLoadFromLock(interp, state_ptr->project_home_dir_ptr, state_ptr->lock_root) != TCL_OK) {
        ttrek_DestroyState(state_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }
    if (!ttrek_RegistryIsLocal()) { ttrek_TelemetryLoadMachineId(interp); }

    if (TCL_OK != ttrek_EnsureGitReady(interp, state_ptr)) {
        fprintf(stderr, "error: ensuring git repository is ready failed\n");
        ttrek_DestroyState(state_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_TouchFile(interp, state_ptr->dirty_file_path_ptr)) {
        fprintf(stderr, "error: creating dirty file failed\n");
        ttrek_DestroyState(state_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    Tcl_Size installObjc = objc - 1;
    Tcl_Obj **installObjv = NULL;
    int do_local_build = 0;

    Tcl_Obj *list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(list_ptr);
    if (installObjc == 0) {
        // add direct dependencies from state_ptr->spec_root to installObjv
        if (TCL_OK != ttrek_GetDirectDependencies(interp, state_ptr->spec_root, list_ptr)) {
            fprintf(stderr, "error: getting direct dependencies failed\n");
            Tcl_DecrRefCount(list_ptr);
            ttrek_DestroyState(state_ptr);
            ckfree(remObjv);
            return TCL_ERROR;
        }

        if (TCL_OK != Tcl_ListObjGetElements(interp, list_ptr, &installObjc, &installObjv)) {
            fprintf(stderr, "error: getting list elements failed\n");
            Tcl_DecrRefCount(list_ptr);
            ttrek_DestroyState(state_ptr);
            ckfree(remObjv);
            return TCL_ERROR;
        }

        do_local_build = 1;
    } else {
        installObjv = &remObjv[1];
    }

    // Set the environment variable IS_TTY to a value that corresponds
    // to whether stdout is terminal. This variable will be used by
    // the installation shell scripts to determine how to show installation
    // progress.
    setenv("IS_TTY", (isatty(1) ? "1" : "0"), 1);
    DBG2(fprintf(stderr, "set IS_TTY: %d", isatty(1)));
    if (option_fail_verbose) {
        setenv("TTREK_FAIL_VERBOSE", "1", 1);
        DBG2(fprintf(stderr, "set TTREK_FAIL_VERBOSE: 1"));
    }

    if (ttrek_InstallRunHook(interp, state_ptr, "preInstall") != TCL_OK) {
        ttrek_DestroyState(state_ptr);
        Tcl_DecrRefCount(list_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    int abort = 0;
    if (TCL_OK != ttrek_InstallOrUpdate(interp, installObjc, installObjv, state_ptr, &abort)) {
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
        fprintf(stderr, "error: removing dirty file failed\n");
        ttrek_DestroyState(state_ptr);
        return TCL_ERROR;
    }

    if (do_local_build) {

        if (TCL_OK != ttrek_RunBuildInstructions(interp, state_ptr)) {
            fprintf(stderr, "error: build instructions failed\n");
            ttrek_DestroyState(state_ptr);
            return TCL_ERROR;
        }

    }

    if (ttrek_InstallRunHook(interp, state_ptr, "postInstall") != TCL_OK) {
        ttrek_DestroyState(state_ptr);
        return TCL_ERROR;
    }

    ttrek_DestroyState(state_ptr);
    return TCL_OK;

}
