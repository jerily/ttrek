/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "ttrek_scripts.h"
#include <string.h>
#include <unistd.h> /* for getpid() */
#include <stdlib.h> /* for getenv() */

int ttrek_ScriptsAdd(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *name_ptr, Tcl_Obj *body_ptr) {

    UNUSED(interp);

    cJSON *scripts_node = cJSON_GetObjectItem(spec_root, "scripts");
    if (scripts_node == NULL) {
        scripts_node = cJSON_CreateObject();
        cJSON_AddItemToObject(spec_root, "scripts", scripts_node);
    }

    const char *body_str = Tcl_GetString(body_ptr);
    cJSON *body_node = cJSON_CreateString(body_str);

    const char *name_str = Tcl_GetString(name_ptr);

    if (cJSON_HasObjectItem(scripts_node, name_str)) {
        cJSON_ReplaceItemInObject(scripts_node, name_str, body_node);
    } else {
        cJSON_AddItemToObject(scripts_node, name_str, body_node);
    }

    return TCL_OK;

}

int ttrek_ScriptsList(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj **list_ptr_ptr) {

    Tcl_Obj *rc = Tcl_NewListObj(0, NULL);
    cJSON *scripts_node = cJSON_GetObjectItem(spec_root, "scripts");

    if (scripts_node != NULL) {

        const cJSON *script_node;

        cJSON_ArrayForEach(script_node, scripts_node) {
            const char *key = script_node->string;
            if (key == NULL) {
                SetResult("failed to get keys from scripts object");
                Tcl_BounceRefCount(rc);
                return TCL_ERROR;
            }
            Tcl_ListObjAppendElement(NULL, rc, Tcl_NewStringObj(key, -1));
        }

    }

    *list_ptr_ptr = rc;
    return TCL_OK;

}

int ttrek_ScriptsDel(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *name_ptr) {

    UNUSED(interp);

    cJSON *scripts_node = cJSON_GetObjectItem(spec_root, "scripts");

    if (scripts_node != NULL) {
        const char *name_str = Tcl_GetString(name_ptr);
        cJSON_DeleteItemFromObject(scripts_node, name_str);
    }

    return TCL_OK;

}

static const char *ttrek_ScriptsGet(cJSON *spec_root, const char *name_str) {

    cJSON *scripts_node = cJSON_GetObjectItem(spec_root, "scripts");
    if (scripts_node != NULL) {

        cJSON *body_node = cJSON_GetObjectItem(scripts_node, name_str);
        if (body_node != NULL) {
            return cJSON_GetStringValue(body_node);
        }

    }

    return NULL;

}

int ttrek_ScriptsDescribe(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *name_ptr, Tcl_Obj **body_ptr_ptr) {

    UNUSED(interp);

    const char *body_str = ttrek_ScriptsGet(spec_root, Tcl_GetString(name_ptr));
    *body_ptr_ptr = (body_str == NULL ? NULL : Tcl_NewStringObj(body_str, -1));

    return TCL_OK;

}

int ttrek_ScriptsMakeTempFile(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *name_str, Tcl_Obj **file_ptr_ptr) {

    const char *body_str = ttrek_ScriptsGet(state_ptr->spec_root, name_str);
    if (body_str == NULL) {
        *file_ptr_ptr = NULL;
        return TCL_OK;
    }

    // Generate temporary file name using pid and current timestamp
    Tcl_Time now;
    Tcl_GetTime(&now);
    Tcl_Obj *temp_file = Tcl_ObjPrintf("%s/script-%ld-%ld.sh",
        Tcl_GetString(state_ptr->project_temp_dir_ptr),
        (long)getpid(),
        now.usec);
    Tcl_IncrRefCount(temp_file);

    Tcl_Channel chan = Tcl_OpenFileChannel(interp, Tcl_GetString(temp_file), "w", 0744);
    if (chan == NULL) {
        // We have an error message from Tcl_OpenFileChannel() in Tcl interp
        Tcl_DecrRefCount(temp_file);
        return TCL_ERROR;
    }

    int body_len = strlen(body_str);
    if (Tcl_WriteChars(chan, body_str, body_len) != body_len) {
        SetResult("could not write script to the file");
        Tcl_DecrRefCount(temp_file);
        return TCL_ERROR;
    }

    Tcl_Close(interp, chan);

    *file_ptr_ptr = temp_file;

    return TCL_OK;

}

int ttrek_ScriptsDefineRunArgs(Tcl_Interp *interp, Tcl_Obj *file_ptr, Tcl_Size *argc_ptr, const char ***argv_ptr) {

    const char *shell = getenv("SHELL");
    if (shell == NULL || strlen(shell) == 0) {
        SetResult("environment variable SHELL is not defined or empty");
        return TCL_ERROR;
    }

    // Construct command line:
    //     $SHELL -c /path/to/file

    Tcl_Size argc = 3;
    const char **argv = ckalloc(sizeof(char *) * (argc + 1));

    argv[0] = shell;
    argv[1] = "-c";
    argv[2] = Tcl_GetString(file_ptr);
    argv[3] = NULL;

    *argc_ptr = argc;
    *argv_ptr = argv;

    return TCL_OK;

}



