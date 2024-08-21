/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include "ttrek_scripts.h"

typedef int (ttrek_ScriptsSubCmdProc)(Tcl_Interp *interp, ttrek_state_t *state_ptr, Tcl_Size objc, Tcl_Obj *const objv[]);

static ttrek_ScriptsSubCmdProc ttrek_ScriptsSubCmdAdd;
static ttrek_ScriptsSubCmdProc ttrek_ScriptsSubCmdDel;
static ttrek_ScriptsSubCmdProc ttrek_ScriptsSubCmdLs;
static ttrek_ScriptsSubCmdProc ttrek_ScriptsSubCmdDescribe;

static const struct {
    const char *cmd;
    ttrek_ScriptsSubCmdProc *proc;
} scripts_commands[] = {
    { "add",      ttrek_ScriptsSubCmdAdd },
    { "del",      ttrek_ScriptsSubCmdDel },
    { "ls",       ttrek_ScriptsSubCmdLs },
    { "describe", ttrek_ScriptsSubCmdDescribe },
    { NULL,       NULL }
};

static int ttrek_ScriptsSubCmdAdd(Tcl_Interp *interp, ttrek_state_t *state_ptr, Tcl_Size objc, Tcl_Obj *const objv[]) {

    if (objc != 2) {
        SetResult("wrong # args: should be \"ttrek scripts add script_name script_body\"");
        return TCL_ERROR;
    }

    ttrek_ScriptsAdd(interp, state_ptr->spec_root, objv[0], objv[1]);
    ttrek_WriteJsonFile(interp, state_ptr->spec_json_path_ptr, state_ptr->spec_root);

    return TCL_OK;

}

static int ttrek_ScriptsSubCmdDel(Tcl_Interp *interp, ttrek_state_t *state_ptr, Tcl_Size objc, Tcl_Obj *const objv[]) {

    if (objc != 1) {
        SetResult("wrong # args: should be \"ttrek scripts del script_name\"");
        return TCL_ERROR;
    }

    ttrek_ScriptsDel(interp, state_ptr->spec_root, objv[0]);
    ttrek_WriteJsonFile(interp, state_ptr->spec_json_path_ptr, state_ptr->spec_root);

    return TCL_OK;

}

static int ttrek_ScriptsSubCmdLs(Tcl_Interp *interp, ttrek_state_t *state_ptr, Tcl_Size objc, Tcl_Obj *const objv[]) {

    UNUSED(objv);

    if (objc != 0) {
        SetResult("wrong # args: should be \"ttrek scripts ls\"");
        return TCL_ERROR;
    }

    Tcl_Obj *script_list = NULL;
    if (ttrek_ScriptsList(interp, state_ptr->spec_root, &script_list) != TCL_OK) {
        return TCL_ERROR;
    }

    Tcl_Size script_list_len;
    Tcl_ListObjLength(interp, script_list, &script_list_len);
    for (Tcl_Size i = 0; i < script_list_len; i++) {
        Tcl_Obj *elem;
        Tcl_ListObjIndex(interp, script_list, i, &elem);
        printf("%s\n", Tcl_GetString(elem));
    }

    Tcl_BounceRefCount(script_list);

    return TCL_OK;

}

static int ttrek_ScriptsSubCmdDescribe(Tcl_Interp *interp, ttrek_state_t *state_ptr, Tcl_Size objc, Tcl_Obj *const objv[]) {

    if (objc != 1) {
        SetResult("wrong # args: should be \"ttrek scripts describe script_name\"");
        return TCL_ERROR;
    }

    Tcl_Obj *script_body = NULL;
    ttrek_ScriptsDescribe(interp, state_ptr->spec_root, objv[0], &script_body);

    if (script_body != NULL) {
        printf("%s\n", Tcl_GetString(script_body));
        Tcl_BounceRefCount(script_body);
    }

    return TCL_OK;

}

/*
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
    if (objc < 1) {
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
*/

#define SCRIPTS_CMD_USAGE "Usage: ttrek scripts ?-g|-u? subcommand ?args?\n" \
    "\n" \
    "Available subcommands:\n" \
    "    add       - adds a named script\n" \
    "    del       - deletes a named script\n" \
    "    ls        - lists defined named scripts\n" \
    "    describe  - describe a named script"

int ttrek_ScriptsSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    int option_user = 0;
    int option_global = 0;
    Tcl_ArgvInfo ArgTable[] = {
        { TCL_ARGV_CONSTANT, "-u", INT2PTR(1), &option_user,   "use scripts for user mode",   NULL },
        { TCL_ARGV_CONSTANT, "-g", INT2PTR(1), &option_global, "use scripts for global mode", NULL },
        { TCL_ARGV_END,      NULL, NULL,       NULL,           NULL,                          NULL }
    };

    Tcl_Obj **remObjv;
    Tcl_ParseArgsObjv(interp, ArgTable, &objc, objv, &remObjv);

    // Skip the command "scripts"
    objc--;
    objv = remObjv + 1;

    if (objc < 1) {
        SetResult("missing subcommand\n\n" SCRIPTS_CMD_USAGE);
        return TCL_ERROR;
    }

    int cmd_idx;
    if (Tcl_GetIndexFromObjStruct(interp, objv[0], scripts_commands,
            sizeof(scripts_commands[0]), "subcommand", 0, &cmd_idx) != TCL_OK)
    {
        return TCL_ERROR;
    }

    if (option_user && option_global) {
        SetResult("conflicting options -u and -g");
        ckfree(remObjv);
        return TCL_ERROR;
    }

    int with_locking = 0;
    ttrek_mode_t mode = option_user ? MODE_USER : (option_global ? MODE_GLOBAL : MODE_LOCAL);
    DBG2(printf("create state..."));
    ttrek_state_t *state_ptr = ttrek_CreateState(interp, 0, 0, with_locking, mode, STRATEGY_LATEST);

    if (!state_ptr) {
        SetResult("initializing ttrek state failed");
        ckfree(remObjv);
        return TCL_ERROR;
    }

    // Skip the subcommand
    objc--;
    objv++;

    DBG2(printf("run subcommand..."));
    int rc = scripts_commands[cmd_idx].proc(interp, state_ptr, objc, objv);

    DBG2(printf("destroy state..."));
    ttrek_DestroyState(state_ptr);
    ckfree(remObjv);
    DBG2(printf("return: %s", (rc == TCL_OK ? "OK" : "ERROR")));
    return rc;

}
