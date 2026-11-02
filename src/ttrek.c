/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <tcl.h>
#include <string.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include "subCmdDecls.h"
#include "common.h"
#include "ttrek_telemetry.h"
#include "registry.h"

static const char *subcommands[] = {
        "init",
        "install",
        "uninstall",
        "run",
        "update",
        "ls",
        /* internal subcommands */
        "download",
        "unpack",
        "help",
        "use-flags",
        "scripts",
        "pretend",
        "gen-script",
        NULL
};

enum subcommand {
    SUBCMD_INIT,
    SUBCMD_INSTALL,
    SUBCMD_UNINSTALL,
    SUBCMD_RUN,
    SUBCMD_UPDATE,
    SUBCMD_LIST,
    SUBCMD_DOWNLOAD,
    SUBCMD_UNPACK,
    SUBCMD_HELP,
    SUBCMD_USE_FLAGS,
    SUBCMD_SCRIPTS,
    SUBCMD_PRETEND,
    SUBCMD_GEN_SCRIPT,
};

int main(int argc, char *argv[]) {

    int exitcode = 0;
    Tcl_Interp *interp = NULL;
    Tcl_Size objc = 0;
    Tcl_Obj **objv;
    CURLcode isCurlInitialized = CURLE_FAILED_INIT;

    if (argc <= 1) {
        goto usage;
    }

    cJSON_Hooks hooks = {
            .malloc_fn = Tcl_Alloc,
            .free_fn = Tcl_Free
    };
    cJSON_InitHooks(&hooks);

    interp = Tcl_CreateInterp();

    // On Windows, argument is not not used. TclpFindExecutable() uses
    // GetModuleFileNameW() to get the executable name.
#ifdef _WIN32
    Tcl_FindExecutable(NULL);
#else
    Tcl_FindExecutable(argv[0]);
#endif

    int first_command = 1;
    while (first_command < argc && strcmp(argv[first_command], "-registry") == 0) {
        if (first_command + 1 >= argc) {
            fprintf(stderr, "error: -registry requires a directory\n");
            exitcode = 1;
            goto done;
        }
        if (ttrek_RegistrySetDirectory(interp, argv[first_command + 1]) != TCL_OK) {
            fprintf(stderr, "error: %s\n", Tcl_GetStringResult(interp));
            exitcode = 1;
            goto done;
        }
        first_command += 2;
    }
    if (first_command >= argc) { goto usage; }

    objc = argc - first_command + 1;
    objv = (Tcl_Obj **) ckalloc(sizeof(Tcl_Obj *) * objc);
    for (Tcl_Size i = 0; i < objc; i++) {
        objv[i] = Tcl_NewStringObj(argv[i == 0 ? 0 : first_command + i - 1], -1);
        Tcl_IncrRefCount(objv[i]);
    }

    int sub_cmd_index;
    if (Tcl_GetIndexFromObj(interp, objv[1], subcommands, "subcommand", 0, &sub_cmd_index) != TCL_OK) {
        fprintf(stderr, "Error: unknown subcommand \"%s\"\n\n", Tcl_GetString(objv[1]));
        goto usage;
    }

    // Init/install/update load telemetry after selecting their registry.
    if (!ttrek_RegistryIsLocal() && sub_cmd_index != SUBCMD_PRETEND && sub_cmd_index != SUBCMD_GEN_SCRIPT &&
        sub_cmd_index != SUBCMD_INIT && sub_cmd_index != SUBCMD_INSTALL && sub_cmd_index != SUBCMD_UPDATE) {
        ttrek_TelemetryLoadMachineId(interp);
    }

    switch ((enum subcommand) sub_cmd_index) {
        case SUBCMD_INIT:
            if (ttrek_InitSubCmd(interp, objc-1, &objv[1]) != TCL_OK) {
                fprintf(stderr, "error: init subcommand failed: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
        case SUBCMD_PRETEND:
            isCurlInitialized = curl_global_init(CURL_GLOBAL_ALL);
            if (ttrek_PretendSubCmd(interp, objc-1, &objv[1]) != TCL_OK) {
                fprintf(stderr, "error: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
        case SUBCMD_GEN_SCRIPT:
            isCurlInitialized = curl_global_init(CURL_GLOBAL_ALL);
            if (ttrek_GenScriptSubCmd(interp, objc-1, &objv[1]) != TCL_OK) {
                fprintf(stderr, "error: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
        case SUBCMD_INSTALL:
            isCurlInitialized = curl_global_init(CURL_GLOBAL_ALL);
            if (TCL_OK != ttrek_InstallSubCmd(interp, objc-1, &objv[1])) {
                fprintf(stderr, "error: install subcommand failed: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
        case SUBCMD_UPDATE:
            isCurlInitialized = curl_global_init(CURL_GLOBAL_ALL);
            if (TCL_OK != ttrek_UpdateSubCmd(interp, objc-1, &objv[1])) {
                fprintf(stderr, "error: update subcommand failed: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
        case SUBCMD_UNINSTALL:
            if (TCL_OK != ttrek_UninstallSubCmd(interp, objc-1, &objv[1])) {
                fprintf(stderr, "error: uninstall subcommand failed: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
        case SUBCMD_RUN:
            if (TCL_OK != ttrek_RunSubCmd(interp, objc-1, &objv[1])) {
                fprintf(stderr, "error: run subcommand failed: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
        case SUBCMD_DOWNLOAD:
            isCurlInitialized = curl_global_init(CURL_GLOBAL_ALL);
            if (TCL_OK != ttrek_DownloadSubCmd(interp, objc-1, &objv[1])) {
                fprintf(stderr, "error: run subcommand failed: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
        case SUBCMD_UNPACK:
            if (TCL_OK != ttrek_UnpackSubCmd(interp, objc-1, &objv[1])) {
                fprintf(stderr, "error: run subcommand failed: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
        case SUBCMD_LIST:
            if (TCL_OK != ttrek_ListSubCmd(interp, objc-1, &objv[1])) {
                fprintf(stderr, "error: list subcommand failed: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
        case SUBCMD_HELP:
            if (TCL_OK != ttrek_HelpSubCmd(interp, objc-1, &objv[1])) {
                exitcode = 1;
            }
            break;
        case SUBCMD_USE_FLAGS:
            if (TCL_OK != ttrek_UseSubCmd(interp, objc-2, &objv[2])) {
                fprintf(stderr, "error: use subcommand failed: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
        case SUBCMD_SCRIPTS:
            if (TCL_OK != ttrek_ScriptsSubCmd(interp, objc-1, &objv[1])) {
                fprintf(stderr, "error: %s\n", Tcl_GetStringResult(interp));
                exitcode = 1;
            }
            break;
    }

    goto done;

usage:
    fprintf(stderr, "Usage: ttrek [-registry directory] <subcommand> [options]\n");
    exitcode = 1;

done:
    // DBG2(fprintf(stderr, "free telemetry..."));
    ttrek_TelemetryFree();
    // DBG2(fprintf(stderr, "free environment state..."));
    ttrek_EnvironmentStateFree();

    if (isCurlInitialized == CURLE_OK) {
        // DBG2(fprintf(stderr, "free curl..."));
        curl_global_cleanup();
    }
    if (objc) {
        for (Tcl_Size i = 0; i < objc; i++) {
            // DBG2(fprintf(stderr, "free objv#%" TCL_SIZE_MODIFIER "d ...", i));
            Tcl_DecrRefCount(objv[i]);
        }
        // DBG2(fprintf(stderr, "free objv"));
        ckfree(objv);
    }
    if (interp != NULL) {
        // DBG2(fprintf(stderr, "delete interp"));
        Tcl_DeleteInterp(interp);
    }
    DBG2(fprintf(stderr, "exit: %d", exitcode));
    return exitcode;
}
