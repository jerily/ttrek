/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include "ttrek_git.h"
#include "ttrek_useflags.h"

static const char *use_commands[] = {
        "add",
        "del",
        "set",
        "ls",
        NULL
};

enum usecommand {
    USECMD_ADD,
    USECMD_DEL,
    USECMD_SET,
    USECMD_GET
};

static int ttrek_UseSubCmdAdd(Tcl_Interp *interp, ttrek_state_t *state_ptr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc < 2) {
        fprintf(stderr, "error: missing use flags\n");
        return TCL_ERROR;
    }

    ttrek_AddUseFlags(interp, state_ptr->spec_root, objc - 1, &objv[1]);
    ttrek_WriteJsonFile(interp, state_ptr->spec_json_path_ptr, state_ptr->spec_root);
    return TCL_OK;
}

static int ttrek_UseSubCmdDel(Tcl_Interp *interp, ttrek_state_t *state_ptr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc < 2) {
        fprintf(stderr, "error: missing use flags\n");
        return TCL_ERROR;
    }

    ttrek_DelUseFlags(interp, state_ptr->spec_root, objc - 1, &objv[1]);
    ttrek_WriteJsonFile(interp, state_ptr->spec_json_path_ptr, state_ptr->spec_root);
    return TCL_OK;
}

static int ttrek_UseSubCmdSet(Tcl_Interp *interp, ttrek_state_t *state_ptr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc < 2) {
        fprintf(stderr, "error: missing use flags\n");
        return TCL_ERROR;
    }

    ttrek_SetUseFlags(interp, state_ptr->spec_root, objc - 1, &objv[1]);
    ttrek_WriteJsonFile(interp, state_ptr->spec_json_path_ptr, state_ptr->spec_root);
    return TCL_OK;
}

static int ttrek_UseSubCmdGet(Tcl_Interp *interp, ttrek_state_t *state_ptr, Tcl_Size objc, Tcl_Obj *const objv[]) {

    UNUSED(objv);

    if (objc > 1) {
        fprintf(stderr, "error: too many arguments\n");
        return TCL_ERROR;
    }

    Tcl_Obj *list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(list_ptr);

    // get all use flags

    if (TCL_OK != ttrek_GetUseFlags(interp, state_ptr->spec_root, list_ptr)) {
        fprintf(stderr, "error: getting use flags failed\n");
        Tcl_DecrRefCount(list_ptr);
        return TCL_ERROR;
    }

    // print the list to stdout
    Tcl_Size list_len;
    if (TCL_OK != Tcl_ListObjLength(interp, list_ptr, &list_len)) {
        fprintf(stderr, "error: getting list length failed\n");
        Tcl_DecrRefCount(list_ptr);
        return TCL_ERROR;
    }

    for (Tcl_Size i = 0; i < list_len; i++) {
        Tcl_Obj *elem;
        if (TCL_OK != Tcl_ListObjIndex(interp, list_ptr, i, &elem)) {
            fprintf(stderr, "error: getting list element failed\n");
            Tcl_DecrRefCount(list_ptr);
            return TCL_ERROR;
        }
        fprintf(stdout, "%s\n", Tcl_GetString(elem));
    }

    Tcl_DecrRefCount(list_ptr);
    return TCL_OK;
}

int ttrek_UseSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    if (objc < 1) {
        fprintf(stderr, "error: missing option\n");
        return TCL_ERROR;
    }

    int use_cmd_index;
    if (Tcl_GetIndexFromObj(interp, objv[0], use_commands, "option", 0, &use_cmd_index) != TCL_OK) {
        fprintf(stderr, "Error: unknown option \"%s\"\n\n", Tcl_GetString(objv[0]));
        return TCL_ERROR;
    }

    int option_user = 0;
    int option_global = 0;
    Tcl_ArgvInfo ArgTable[] = {
            {TCL_ARGV_CONSTANT, "-u",        INT2PTR(1), &option_user,     "set use flags for user mode",                                          NULL},
            {TCL_ARGV_CONSTANT, "-g",        INT2PTR(1), &option_global,   "set use flags for global mode",                                        NULL},
            {TCL_ARGV_END,      NULL,        NULL,       NULL,             NULL,                                                                   NULL}
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


    int result;
    switch ((enum usecommand) use_cmd_index) {
        case USECMD_ADD:
            result = ttrek_UseSubCmdAdd(interp, state_ptr, objc, objv);
            break;
        case USECMD_DEL:
            result = ttrek_UseSubCmdDel(interp, state_ptr, objc, objv);
            break;
        case USECMD_SET:
            result = ttrek_UseSubCmdSet(interp, state_ptr, objc, objv);
            break;
        case USECMD_GET:
            result = ttrek_UseSubCmdGet(interp, state_ptr, objc, objv);
            break;
        default:
            fprintf(stderr, "Error: unknown option \"%s\"\n\n", Tcl_GetString(objv[0]));
            result = TCL_ERROR;
            break;
    }

    ttrek_DestroyState(state_ptr);
    ckfree(remObjv);
    return result;
}
