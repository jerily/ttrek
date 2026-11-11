/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include "ttrek_scripts.h"

typedef int (ttrek_ScriptsSubCmdProc)(Tcl_Interp *interp, ttrek_state_t *state_ptr, cJSON *source, Tcl_Size objc, Tcl_Obj *const objv[]);

static ttrek_ScriptsSubCmdProc ttrek_ScriptsSubCmdAdd;
static ttrek_ScriptsSubCmdProc ttrek_ScriptsSubCmdDel;
static ttrek_ScriptsSubCmdProc ttrek_ScriptsSubCmdLs;
static ttrek_ScriptsSubCmdProc ttrek_ScriptsSubCmdDescribe;

static const struct {
    const char *cmd;
    ttrek_ScriptsSubCmdProc *proc;
    int modifies_project;
} scripts_commands[] = {
    { "add",      ttrek_ScriptsSubCmdAdd, 1 },
    { "del",      ttrek_ScriptsSubCmdDel, 1 },
    { "ls",       ttrek_ScriptsSubCmdLs, 0 },
    { "describe", ttrek_ScriptsSubCmdDescribe, 0 },
    { NULL,       NULL, 0 }
};

static int ttrek_ScriptsSubCmdAdd(Tcl_Interp *interp, ttrek_state_t *state_ptr, cJSON *source, Tcl_Size objc, Tcl_Obj *const objv[]) {

    if (objc != 2) {
        SetResult("wrong # args: should be \"ttrek scripts add script_name script_body\"");
        return TCL_ERROR;
    }

    ttrek_ScriptsAdd(interp, source, objv[0], objv[1]);
    ttrek_WriteJsonFile(interp, state_ptr->spec_json_path_ptr, state_ptr->spec_root);

    return TCL_OK;

}

static int ttrek_ScriptsSubCmdDel(Tcl_Interp *interp, ttrek_state_t *state_ptr, cJSON *source, Tcl_Size objc, Tcl_Obj *const objv[]) {

    if (objc != 1) {
        SetResult("wrong # args: should be \"ttrek scripts del script_name\"");
        return TCL_ERROR;
    }

    ttrek_ScriptsDel(interp, source, objv[0]);
    ttrek_WriteJsonFile(interp, state_ptr->spec_json_path_ptr, state_ptr->spec_root);

    return TCL_OK;

}

static int ttrek_ScriptsSubCmdLs(Tcl_Interp *interp, ttrek_state_t *state_ptr, cJSON *source, Tcl_Size objc, Tcl_Obj *const objv[]) {

    UNUSED(objv);
    UNUSED(state_ptr);

    if (objc != 0) {
        SetResult("wrong # args: should be \"ttrek scripts ls\"");
        return TCL_ERROR;
    }

    Tcl_Obj *script_list = NULL;
    if (ttrek_ScriptsList(interp, source, &script_list) != TCL_OK) {
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

static int ttrek_ScriptsSubCmdDescribe(Tcl_Interp *interp, ttrek_state_t *state_ptr, cJSON *source, Tcl_Size objc, Tcl_Obj *const objv[]) {

    if (objc != 1) {
        SetResult("wrong # args: should be \"ttrek scripts describe script_name\"");
        return TCL_ERROR;
    }

    Tcl_Obj *script_body = NULL;
    if (ttrek_ScriptsDescribe(interp, state_ptr, source, Tcl_GetString(objv[0]), &script_body) != TCL_OK) {
        return TCL_ERROR;
    }

    if (script_body == NULL && source != state_ptr->spec_root) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("there is no named script \"%s\" in the installed package", Tcl_GetString(objv[0])));
        return TCL_ERROR;
    }
    if (script_body != NULL) {
        printf("%s\n", Tcl_GetString(script_body));
        Tcl_DecrRefCount(script_body);
    }

    return TCL_OK;

}

#define SCRIPTS_CMD_USAGE "Usage: ttrek scripts ?-g|-u? ?-package PACKAGE? subcommand ?args?\n" \
    "\n" \
    "Available subcommands:\n" \
    "    add       - adds a named script\n" \
    "    del       - deletes a named script\n" \
    "    ls        - lists defined named scripts\n" \
    "    describe  - describe a named script"

int ttrek_ScriptsSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    int option_user = 0;
    int option_global = 0;
    const char *option_package = NULL;
    Tcl_ArgvInfo ArgTable[] = {
        { TCL_ARGV_CONSTANT, "-u", INT2PTR(1), &option_user,   "use scripts for user mode",   NULL },
        { TCL_ARGV_CONSTANT, "-g", INT2PTR(1), &option_global, "use scripts for global mode", NULL },
        { TCL_ARGV_STRING, "-package", NULL, &option_package, "inspect scripts from an installed package", NULL },
        { TCL_ARGV_END,      NULL, NULL,       NULL,           NULL,                          NULL }
    };

    Tcl_Obj **remObjv = NULL;
    if (Tcl_ParseArgsObjv(interp, ArgTable, &objc, objv, &remObjv) != TCL_OK) {
        ckfree(remObjv);
        return TCL_ERROR;
    }

    // Skip the command "scripts"
    objc--;
    objv = remObjv + 1;

    if (objc < 1) {
        SetResult("missing subcommand\n\n" SCRIPTS_CMD_USAGE);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    int cmd_idx;
    if (Tcl_GetIndexFromObjStruct(interp, objv[0], scripts_commands,
            sizeof(scripts_commands[0]), "subcommand", 0, &cmd_idx) != TCL_OK)
    {
        ckfree(remObjv);
        return TCL_ERROR;
    }

    if (option_user && option_global) {
        SetResult("conflicting options -u and -g");
        ckfree(remObjv);
        return TCL_ERROR;
    }

    if (option_package && (!option_package[0] || scripts_commands[cmd_idx].modifies_project)) {
        SetResult("-package requires a package name and supports only ls or describe; package scripts are read-only");
        ckfree(remObjv);
        return TCL_ERROR;
    }

    int with_locking = 0;
    ttrek_mode_t mode = option_user ? MODE_USER : (option_global ? MODE_GLOBAL : MODE_LOCAL);
    DBG2(fprintf(stderr, "create state..."));
    ttrek_state_t *state_ptr = ttrek_CreateState(interp, 0, 0, with_locking, mode, STRATEGY_LATEST);

    if (!state_ptr) {
        SetResult("initializing ttrek state failed");
        ckfree(remObjv);
        return TCL_ERROR;
    }

    cJSON *source = state_ptr->spec_root;
    if (option_package && ttrek_ScriptsPackageSource(interp, state_ptr, option_package, &source) != TCL_OK) {
        ttrek_DestroyState(state_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }

    // Skip the subcommand
    objc--;
    objv++;

    DBG2(fprintf(stderr, "run subcommand..."));
    int rc = scripts_commands[cmd_idx].proc(interp, state_ptr, source, objc, objv);

    DBG2(fprintf(stderr, "destroy state..."));
    ttrek_DestroyState(state_ptr);
    ckfree(remObjv);
    DBG2(fprintf(stderr, "return: %s", (rc == TCL_OK ? "OK" : "ERROR")));
    return rc;

}
