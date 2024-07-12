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
    SUBCMD_HELP
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

    ttrek_TelemetryLoadMachineId(interp);

    objc = argc;
    objv = (Tcl_Obj **) ckalloc(sizeof(Tcl_Obj *) * argc);
    for (Tcl_Size i = 0; i < argc; i++) {
        objv[i] = Tcl_NewStringObj(argv[i], -1);
        Tcl_IncrRefCount(objv[i]);
    }

    int sub_cmd_index;
    if (Tcl_GetIndexFromObj(interp, objv[1], subcommands, "subcommand", 0, &sub_cmd_index) != TCL_OK) {
        fprintf(stderr, "Error: unknown subcommand \"%s\"\n\n", Tcl_GetString(objv[1]));
        goto usage;
    }

    switch ((enum subcommand) sub_cmd_index) {
        case SUBCMD_INIT:
            fprintf(stderr, "init\n");
            ttrek_InitSubCmd(interp, objc-1, &objv[1]);
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
    }

    goto done;

usage:
    fprintf(stderr, "Usage: ttrek <subcommand> [options]\n");
    exitcode = 1;

done:
    ttrek_TelemetryFree();

    if (isCurlInitialized == CURLE_OK) {
        curl_global_cleanup();
    }
    if (objc) {
        for (Tcl_Size i = 0; i < objc; i++) {
            Tcl_DecrRefCount(objv[i]);
        }
        ckfree(objv);
    }
    if (interp != NULL) {
        Tcl_DeleteInterp(interp);
    }
    return exitcode;
}
