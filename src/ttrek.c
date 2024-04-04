/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <tcl.h>
#include <string.h>
#include <cjson/cJSON.h>
#include "subCmdDecls.h"s
#include "common.h"s

static const char *subcommands[] = {
        "init",
        "install",
        "uninstall",
        "run",
        NULL
};

enum subcommand {
    SUBCMD_INIT,
    SUBCMD_INSTALL,
    SUBCMD_UNINSTALL,
    SUBCMD_RUN
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
                fprintf(stderr, "install\n");
                ttrek_InstallSubCmd(interp, objc-1, &objv[1]);
                break;
            case SUBCMD_UNINSTALL:
                fprintf(stderr, "uninstall\n");
                break;
            case SUBCMD_RUN:
                ttrek_RunSubCmd(interp, objc-2, &objv[2]);
                fprintf(stderr, "run\n");
                break;
        }
    }

    return 0;
}