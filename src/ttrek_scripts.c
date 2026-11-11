/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "ttrek_scripts.h"
#include <string.h>
#include <unistd.h> /* for getpid() */
#include <stdlib.h> /* for getenv() */
#include <sys/stat.h>

int ttrek_ScriptsValidate(Tcl_Interp *interp, const cJSON *scripts) {
    if (!cJSON_IsObject(scripts)) {
        SetResult("recipe scripts must be an object of named shell script strings");
        return TCL_ERROR;
    }
    const cJSON *script;
    cJSON_ArrayForEach(script, scripts) {
        if (!script->string || !script->string[0] || !cJSON_IsString(script)) {
            SetResult("recipe scripts must have nonempty names and string bodies");
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

int ttrek_ScriptsPackageSource(Tcl_Interp *interp, ttrek_state_t *state_ptr,
        const char *package_name, cJSON **source_ptr) {
    cJSON *package = cJSON_GetObjectItemCaseSensitive(state_ptr->manifest_root, package_name);
    if (!cJSON_IsObject(package)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("package \"%s\" is not installed in this environment", package_name));
        return TCL_ERROR;
    }
    cJSON *scripts = cJSON_GetObjectItemCaseSensitive(package, "scripts");
    cJSON *version = cJSON_GetObjectItemCaseSensitive(package, "version");
    if (!scripts || !cJSON_IsString(version) || !version->valuestring[0]) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("package \"%s\" has no cached recipe scripts; "
            "reinstall it with 'ttrek install -force %s' in this environment", package_name, package_name));
        return TCL_ERROR;
    }
    if (ttrek_ScriptsValidate(interp, scripts) != TCL_OK) {
        return TCL_ERROR;
    }
    *source_ptr = package;
    return TCL_OK;
}

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

int ttrek_ScriptsDescribe(Tcl_Interp *interp, ttrek_state_t *state_ptr, cJSON *source,
                          const char *name, Tcl_Obj **body_ptr_ptr) {
    *body_ptr_ptr = NULL;
    cJSON *scripts = cJSON_GetObjectItem(source, "scripts");
    if (!scripts) { return TCL_OK; }
    if (!cJSON_IsObject(scripts)) {
        SetResult("scripts must be an object");
        return TCL_ERROR;
    }
    cJSON *script = cJSON_GetObjectItem(scripts, name);
    if (!script) { return TCL_OK; }
    if (cJSON_IsString(script)) {
        *body_ptr_ptr = Tcl_NewStringObj(script->valuestring, -1);
        Tcl_IncrRefCount(*body_ptr_ptr);
        return TCL_OK;
    }

    cJSON *file = cJSON_GetObjectItemCaseSensitive(script, "file");
    cJSON *args = cJSON_GetObjectItemCaseSensitive(script, "args");
    if (source != state_ptr->spec_root || !cJSON_IsObject(script) || cJSON_GetArraySize(script) != (args ? 2 : 1) ||
        !cJSON_IsString(file) || !file->valuestring[0]) {
        SetResult("project scripts must be strings or objects containing a nonempty file path and optional args");
        return TCL_ERROR;
    }
    if (args && !cJSON_IsArray(args)) {
        SetResult("script args must be an array of strings");
        return TCL_ERROR;
    }
    const cJSON *arg;
    cJSON_ArrayForEach(arg, args) {
        if (!cJSON_IsString(arg)) {
            SetResult("script args must be an array of strings");
            return TCL_ERROR;
        }
    }
    Tcl_Obj *relative = Tcl_NewStringObj(file->valuestring, -1);
    Tcl_IncrRefCount(relative);
    int valid = Tcl_FSGetPathType(relative) == TCL_PATH_RELATIVE;
    Tcl_Size count;
    Tcl_Obj *parts = Tcl_FSSplitPath(relative, &count);
    Tcl_IncrRefCount(parts);
    for (Tcl_Size i = 0; i < count; ++i) {
        Tcl_Obj *part;
        Tcl_ListObjIndex(NULL, parts, i, &part);
        if (strcmp(Tcl_GetString(part), "..") == 0) { valid = 0; }
    }
    Tcl_DecrRefCount(parts);
    Tcl_DecrRefCount(relative);
    if (!valid) {
        SetResult("script file must be a relative path without '..'");
        return TCL_ERROR;
    }

    Tcl_Obj *path = ttrek_GetFilePath(interp, state_ptr->project_home_dir_ptr, file->valuestring);
    if (!path) { return TCL_ERROR; }
    Tcl_StatBuf statbuf;
    if (Tcl_FSLstat(path, &statbuf) != 0 || !S_ISREG(statbuf.st_mode)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("script file must be an existing regular file: %s", Tcl_GetString(path)));
        Tcl_DecrRefCount(path);
        return TCL_ERROR;
    }
    Tcl_Channel channel = Tcl_FSOpenFileChannel(interp, path, "r", 0);
    Tcl_DecrRefCount(path);
    if (!channel) { return TCL_ERROR; }
    Tcl_Obj *body = Tcl_NewObj();
    Tcl_IncrRefCount(body);
    if (Tcl_SetChannelOption(interp, channel, "-encoding", "utf-8") != TCL_OK ||
        Tcl_ReadChars(channel, body, -1, 0) < 0) {
        Tcl_Close(NULL, channel);
        Tcl_DecrRefCount(body);
        return TCL_ERROR;
    }
    if (Tcl_Close(interp, channel) != TCL_OK) {
        Tcl_DecrRefCount(body);
        return TCL_ERROR;
    }
    const Tcl_UniChar *characters = Tcl_GetUnicodeFromObj(body, &count);
    for (Tcl_Size i = 0; i < count; ++i) {
        if (characters[i] == 0) {
            SetResult("script file must not contain NUL bytes");
            Tcl_DecrRefCount(body);
            return TCL_ERROR;
        }
    }
    *body_ptr_ptr = body;
    return TCL_OK;
}

int ttrek_ScriptsPrepareRun(Tcl_Interp *interp, ttrek_state_t *state_ptr, cJSON *source,
                           const char *name, ttrek_script_t *script) {

    memset(script, 0, sizeof(*script));
    Tcl_Obj *body = NULL;
    if (ttrek_ScriptsDescribe(interp, state_ptr, source, name, &body) != TCL_OK) { return TCL_ERROR; }
    if (!body) { return TCL_OK; }
    cJSON *definition = cJSON_GetObjectItem(cJSON_GetObjectItem(source, "scripts"), name);
    if (cJSON_IsObject(definition)) {
        // Execute the original file so $0 and BASH_SOURCE still locate its siblings.
        const cJSON *file = cJSON_GetObjectItemCaseSensitive(definition, "file");
        script->path = ttrek_GetFilePath(interp, state_ptr->project_home_dir_ptr, file->valuestring);
        script->args = cJSON_GetObjectItemCaseSensitive(definition, "args");
        Tcl_DecrRefCount(body);
        if (!script->path) { return TCL_ERROR; }
        if (Tcl_FSAccess(script->path, X_OK) != 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("script file is not executable: %s", Tcl_GetString(script->path)));
            ttrek_ScriptsRelease(script);
            return TCL_ERROR;
        }
        return TCL_OK;
    }
    // Recovery from a failed install may have removed the untracked temp directory.
    if (ttrek_EnsureDirectoryExists(interp, state_ptr->project_temp_dir_ptr) != TCL_OK) {
        Tcl_DecrRefCount(body);
        return TCL_ERROR;
    }

    // Generate temporary file name using pid and current timestamp
    Tcl_Time now;
    Tcl_GetTime(&now);
    Tcl_Obj *temp_file = Tcl_ObjPrintf("%s/script-%ld-%ld.sh",
        Tcl_GetString(state_ptr->project_temp_dir_ptr),
        (long)getpid(),
        now.usec);
    Tcl_IncrRefCount(temp_file);

    int status = ttrek_WriteChars(interp, temp_file, body, 0744);
    Tcl_DecrRefCount(body);
    if (status != TCL_OK) {
        Tcl_FSDeleteFile(temp_file);
        Tcl_DecrRefCount(temp_file);
        return TCL_ERROR;
    }

    script->path = temp_file;
    script->temporary = 1;

    return TCL_OK;

}

void ttrek_ScriptsRelease(ttrek_script_t *script) {
    if (script->path) {
        if (script->temporary) { Tcl_FSDeleteFile(script->path); }
        Tcl_DecrRefCount(script->path);
    }
    memset(script, 0, sizeof(*script));
}

int ttrek_ScriptsDefineRunArgs(Tcl_Interp *interp, const ttrek_script_t *script,
                              Tcl_Size *argc_ptr, const char ***argv_ptr) {

    const char *shell = getenv("SHELL");
    if (shell == NULL || strlen(shell) == 0) {
        SetResult("environment variable SHELL is not defined or empty");
        return TCL_ERROR;
    }

    // Pass the path as an argument so spaces and shell metacharacters are literal.
    // Executing the file preserves its shebang, or the shell's script fallback.

    Tcl_Size argc = 5 + cJSON_GetArraySize(script->args);
    const char **argv = ckalloc(sizeof(char *) * (argc + 1));

    argv[0] = shell;
    argv[1] = "-c";
    argv[2] = "exec \"$@\"";
    argv[3] = "ttrek-script";
    argv[4] = Tcl_GetString(script->path);
    Tcl_Size index = 5;
    const cJSON *arg;
    cJSON_ArrayForEach(arg, script->args) { argv[index++] = arg->valuestring; }
    argv[argc] = NULL;

    *argc_ptr = argc;
    *argv_ptr = argv;

    return TCL_OK;

}



