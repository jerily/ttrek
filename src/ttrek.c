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

static const char *subcommands[] = {
        "init",
        "install",
        "uninstall",
        "run",
        "pretend",
        NULL
};

enum subcommand {
    SUBCMD_INIT,
    SUBCMD_INSTALL,
    SUBCMD_UNINSTALL,
    SUBCMD_RUN,
    SUBCMD_PRETEND
};

int main(int argc, char *argv[]) {

    if (argc <= 1) {
        fprintf(stderr, "Usage: ttrek <subcommand> [options]\n");
        return 1;
    }

    Tcl_Interp *interp = Tcl_CreateInterp();
    Tcl_Size objc = argc;
    Tcl_Obj **objv = (Tcl_Obj **) Tcl_Alloc(sizeof(Tcl_Obj *) * argc);
    for (int i = 0; i < argc; i++) {
        objv[i] = Tcl_NewStringObj(argv[i], -1);
    }

    int sub_cmd_index;
    if (TCL_OK == Tcl_GetIndexFromObj(interp, objv[1], subcommands, "subcommand", 0, &sub_cmd_index)) {
        switch ((enum subcommand) sub_cmd_index) {
            case SUBCMD_INIT:
                fprintf(stderr, "init\n");
                ttrek_InitSubCmd(interp, objc-1, &objv[1]);
                break;
            case SUBCMD_INSTALL:
                curl_global_init(CURL_GLOBAL_ALL);
                fprintf(stderr, "install\n");
                if (TCL_OK != ttrek_InstallSubCmd(interp, objc-2, &objv[2])) {
                    fprintf(stderr, "error: install subcommand failed: %s\n", Tcl_GetStringResult(interp));
                    curl_global_cleanup();
                    return 1;
                }
                curl_global_cleanup();
                break;
            case SUBCMD_UNINSTALL:
                fprintf(stderr, "uninstall\n");
                break;
            case SUBCMD_RUN:
                if (TCL_OK != ttrek_RunSubCmd(interp, objc-2, &objv[2])) {
                    fprintf(stderr, "error: run subcommand failed: %s\n", Tcl_GetStringResult(interp));
                    return 1;
                }
                break;
            case SUBCMD_PRETEND:
                fprintf(stderr, "pretend\n");
                if (TCL_OK != ttrek_PretendSubCmd(interp, objc-2, &objv[2])) {
                    fprintf(stderr, "error: pretend subcommand failed: %s\n", Tcl_GetStringResult(interp));
                    return 1;
                }
                break;
        }
    }

    return 0;
}