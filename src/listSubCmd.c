/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include "ttrek_git.h"

static int ttrek_GetLockPackages(Tcl_Interp *interp, cJSON *lock_root, Tcl_Obj *list_ptr) {

    if (!cJSON_HasObjectItem(lock_root, "packages")) {
        return TCL_ERROR;
    }

    cJSON *packages = cJSON_GetObjectItem(lock_root, "packages");

    for (int i = 0; i < cJSON_GetArraySize(packages); i++) {
        cJSON *package = cJSON_GetArrayItem(packages, i);
        char package_str[256];
        snprintf(package_str, 256, "%s@%s", package->string, cJSON_GetObjectItem(package, "version")->valuestring);
        Tcl_Obj *package_name = Tcl_NewStringObj(package_str, -1);
        if (TCL_OK != Tcl_ListObjAppendElement(interp, list_ptr, package_name)) {
            return TCL_ERROR;
        }
    }


    return TCL_OK;
}

static int ttrek_GetLockPackagesThatMatch(Tcl_Interp *interp, cJSON *lock_root, Tcl_Obj *pattern, Tcl_Obj *list_ptr) {

    if (!cJSON_HasObjectItem(lock_root, "packages")) {
        return TCL_ERROR;
    }

    cJSON *packages = cJSON_GetObjectItem(lock_root, "packages");

    for (int i = 0; i < cJSON_GetArraySize(packages); i++) {
        cJSON *package = cJSON_GetArrayItem(packages, i);
        if (Tcl_StringMatch(package->string, Tcl_GetString(pattern))) {
            char package_str[256];
            snprintf(package_str, 256, "%s@%s", package->string, cJSON_GetObjectItem(package, "version")->valuestring);
            Tcl_Obj *package_name = Tcl_NewStringObj(package_str, -1);
            if (TCL_OK != Tcl_ListObjAppendElement(interp, list_ptr, package_name)) {
                return TCL_ERROR;
            }
        }
    }

    return TCL_OK;
}

int ttrek_ListSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    int option_user = 0;
    int option_global = 0;
    Tcl_ArgvInfo ArgTable[] = {
            {TCL_ARGV_CONSTANT, "-u",        INT2PTR(1), &option_user,     "run in user mode",                                          NULL},
            {TCL_ARGV_CONSTANT, "-g",        INT2PTR(1), &option_global,   "run in global mode",                                        NULL},
            {TCL_ARGV_END, NULL,             NULL, NULL, NULL}
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

    Tcl_Obj *list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(list_ptr);

    if (objc == 1) {
        if (TCL_OK != ttrek_GetLockPackages(interp, state_ptr->lock_root, list_ptr)) {
            fprintf(stderr, "error: getting direct dependencies failed\n");
            Tcl_DecrRefCount(list_ptr);
            ttrek_DestroyState(state_ptr);
            ckfree(remObjv);
            return TCL_ERROR;
        }
    } else {
        for (Tcl_Size i = 1; i < objc; i++) {
            if (TCL_OK != ttrek_GetLockPackagesThatMatch(interp, state_ptr->lock_root, remObjv[i], list_ptr)) {
                fprintf(stderr, "error: getting direct dependencies failed\n");
                Tcl_DecrRefCount(list_ptr);
                ttrek_DestroyState(state_ptr);
                ckfree(remObjv);
                return TCL_ERROR;
            }
        }
    }

    // print the list to stdout
    Tcl_Size list_len;
    if (TCL_OK != Tcl_ListObjLength(interp, list_ptr, &list_len)) {
        fprintf(stderr, "error: getting list length failed\n");
        Tcl_DecrRefCount(list_ptr);
        ttrek_DestroyState(state_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    for (Tcl_Size i = 0; i < list_len; i++) {
        Tcl_Obj *elem;
        if (TCL_OK != Tcl_ListObjIndex(interp, list_ptr, i, &elem)) {
            fprintf(stderr, "error: getting list element failed\n");
            Tcl_DecrRefCount(list_ptr);
            ttrek_DestroyState(state_ptr);
            ckfree(remObjv);
            return TCL_ERROR;
        }
        fprintf(stdout, "%s\n", Tcl_GetString(elem));
    }

    Tcl_DecrRefCount(list_ptr);

    ttrek_DestroyState(state_ptr);
    ckfree(remObjv);
    return TCL_OK;
}
