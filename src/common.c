/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <tcl.h>
#include <unistd.h>
#include "common.h"
#include "cjson/cJSON.h"

int ttrek_ResolvePath(Tcl_Interp *interp, Tcl_Obj *filename_ptr, Tcl_Obj **path_ptr) {
    Tcl_Obj *current_working_directory = Tcl_FSGetCwd(interp);
    if (!current_working_directory) {
        fprintf(stderr, "error: getting current working directory failed\n");
        return TCL_ERROR;
    }

    Tcl_Obj *objv[1] = {filename_ptr};
    *path_ptr = Tcl_FSJoinToPath(current_working_directory, 1, objv);

    if (!*path_ptr) {
        fprintf(stderr, "error: could not resolve path for %s\n", Tcl_GetString(filename_ptr));
        return TCL_ERROR;
    }
    return TCL_OK;
}

int ttrek_CheckFileExists(Tcl_Obj *path_ptr) {
    if (Tcl_FSAccess(path_ptr, F_OK) != 0) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

int ttrek_WriteJsonFile(Tcl_Interp *interp, Tcl_Obj *path_ptr, const cJSON *root) {
    char buffer[65536];
    cJSON_PrintPreallocated(root, buffer, sizeof(buffer), 1);

    Tcl_Channel chan = Tcl_OpenFileChannel(interp, Tcl_GetString(path_ptr), "w", 0666);
    Tcl_WriteChars(chan, buffer, -1);
    Tcl_Close(interp, chan);
}