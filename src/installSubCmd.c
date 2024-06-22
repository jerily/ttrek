/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "subCmdDecls.h"
#include "common.h"
#include "base64.h"
#include "registry.h"
#include "semver/semver.h"
#include "ttrek_resolvo.h"

#define MAX_INSTALL_SCRIPT_LEN 1048576

int ttrek_InstallSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *project_home_dir_ptr = ttrek_GetProjectHomeDir(interp);
    if (!project_home_dir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return TCL_ERROR;
    }

    fprintf(stderr, "project_home_dir: %s\n", Tcl_GetString(project_home_dir_ptr));

    Tcl_Obj *spec_file_name_ptr = Tcl_NewStringObj(SPEC_JSON_FILE, -1);
    Tcl_IncrRefCount(spec_file_name_ptr);
    Tcl_Obj *path_to_spec_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_home_dir_ptr, spec_file_name_ptr, &path_to_spec_file_ptr)) {
        Tcl_DecrRefCount(spec_file_name_ptr);
        Tcl_DecrRefCount(project_home_dir_ptr);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(spec_file_name_ptr);
    Tcl_IncrRefCount(path_to_spec_file_ptr);

    if (TCL_OK != ttrek_CheckFileExists(path_to_spec_file_ptr)) {
        fprintf(stderr, "error: %s does not exist, run 'ttrek init' first\n", SPEC_JSON_FILE);
        Tcl_DecrRefCount(project_home_dir_ptr);
        Tcl_DecrRefCount(path_to_spec_file_ptr);
        return TCL_ERROR;
    }

    int option_save_dev = 0;
    int option_user = 0;
    int option_global = 0;
    Tcl_ArgvInfo ArgTable[] = {
//            {TCL_ARGV_CONSTANT, "-save-dev", INT2PTR(1), &option_save_dev, "Save the package to the local repository as a dev dependency"},
            {TCL_ARGV_CONSTANT, "-u", INT2PTR(1), &option_user, "install as a user package"},
            {TCL_ARGV_CONSTANT, "-g", INT2PTR(1), &option_global, "install as a global package"},
            {TCL_ARGV_END, NULL, NULL, NULL, NULL}
    };

    Tcl_Obj **remObjv;
    Tcl_ParseArgsObjv(interp, ArgTable, &objc, objv, &remObjv);

    if (TCL_OK != ttrek_install(interp, objc, remObjv)) {
        Tcl_DecrRefCount(project_home_dir_ptr);
        Tcl_DecrRefCount(path_to_spec_file_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    Tcl_DecrRefCount(project_home_dir_ptr);
    Tcl_DecrRefCount(path_to_spec_file_ptr);
    ckfree(remObjv);

    return TCL_OK;
}
