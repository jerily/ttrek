/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include "ttrek_git.h"

static int ttrek_GetUseFlags(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *list_ptr) {

    if (!cJSON_HasObjectItem(spec_root, "defaultUseFlags")) {
        return TCL_OK;
    }

    cJSON *use = cJSON_GetObjectItem(spec_root, "defaultUseFlags");

    for (int i = 0; i < cJSON_GetArraySize(use); i++) {
        cJSON *use_flag_node = cJSON_GetArrayItem(use, i);
        Tcl_Obj *use_flag = Tcl_NewStringObj(use_flag_node->valuestring, -1);
        if (TCL_OK != Tcl_ListObjAppendElement(interp, list_ptr, use_flag)) {
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}

static int ttrek_SetUseFlags(Tcl_Interp *interp, cJSON *spec_root, Tcl_Size objc, Tcl_Obj *const objv[], int *changed) {

    cJSON *use_node = cJSON_CreateArray();

    for (Tcl_Size i = 0; i < objc; i++) {
        cJSON_AddItemToArray(use_node, cJSON_CreateString(Tcl_GetString(objv[i])));
    }

    if (!cJSON_HasObjectItem(spec_root, "defaultUseFlags")) {
        cJSON_AddItemToObject(spec_root, "defaultUseFlags", use_node);
    } else {
        cJSON_ReplaceItemInObject(spec_root, "defaultUseFlags", use_node);
    }

    return TCL_OK;
}

int ttrek_UseSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    int option_user = 0;
    int option_global = 0;
    Tcl_ArgvInfo ArgTable[] = {
            {TCL_ARGV_CONSTANT, "-u",        INT2PTR(1), &option_user,     "set use flags for user mode",                                          NULL},
            {TCL_ARGV_CONSTANT, "-g",        INT2PTR(1), &option_global,   "set use flags for global mode",                                        NULL},
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

    if (objc == 1) {
        Tcl_Obj *list_ptr = Tcl_NewListObj(0, NULL);
        Tcl_IncrRefCount(list_ptr);

        // get all use flags

        if (TCL_OK != ttrek_GetUseFlags(interp, state_ptr->spec_root, list_ptr)) {
            fprintf(stderr, "error: getting use flags failed\n");
            Tcl_DecrRefCount(list_ptr);
            ttrek_DestroyState(state_ptr);
            ckfree(remObjv);
            return TCL_ERROR;
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

    } else {
        // set use flags
        int changed = 0;
        ttrek_SetUseFlags(interp, state_ptr->spec_root, objc - 1, &remObjv[1], &changed);
        ttrek_WriteJsonFile(interp, state_ptr->spec_json_path_ptr, state_ptr->spec_root);
    }

    ttrek_DestroyState(state_ptr);
    ckfree(remObjv);
    return TCL_OK;
}
