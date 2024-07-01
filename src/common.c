/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "common.h"
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cjson/cJSON.h"

static int tjson_TreeToJson(Tcl_Interp *interp, cJSON *item, int num_spaces, Tcl_DString *dsPtr);

int ttrek_ResolvePath(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj *filename_ptr, Tcl_Obj **output_path_ptr) {
    Tcl_Obj *objv[1] = {filename_ptr};
    *output_path_ptr = Tcl_FSJoinToPath(path_ptr, 1, objv);

    if (!*output_path_ptr) {
        fprintf(stderr, "error: could not resolve path for %s\n", Tcl_GetString(filename_ptr));
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(*output_path_ptr);
//    *path_ptr = Tcl_FSGetNormalizedPath(interp, *path_ptr);

    return TCL_OK;
}

int ttrek_CheckFileExists(Tcl_Obj *path_ptr) {
    if (Tcl_FSAccess(path_ptr, F_OK) != 0) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

static int ttrek_FileExists(Tcl_Interp *interp, Tcl_Obj *path_ptr, int *exists) {
    if (TCL_OK != ttrek_CheckFileExists(path_ptr)) {
        *exists = 0;
    } else {
        *exists = 1;
    }
    return TCL_OK;
}

static int ttrek_EnsureDirectoryExists(Tcl_Interp *interp, Tcl_Obj *dir_path_ptr) {
    int exists;
    if (TCL_OK != ttrek_FileExists(interp, dir_path_ptr, &exists)) {
        fprintf(stderr, "error: could not check if directory exists\n");
        return TCL_ERROR;
    }

    if (exists) {
        return TCL_OK;
    }

    if (TCL_OK != Tcl_FSCreateDirectory(dir_path_ptr)) {
        fprintf(stderr, "error: could not create directory\n");
        return TCL_ERROR;
    }

    return TCL_OK;
}

int ttrek_EnsureSkeletonExists(Tcl_Interp *interp, ttrek_state_t *state_ptr) {
    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, state_ptr->project_home_dir_ptr)) {
        fprintf(stderr, "error: could not ensure project home directory exists\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, state_ptr->project_venv_dir_ptr)) {
        fprintf(stderr, "error: could not ensure project venv directory exists\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, state_ptr->project_build_dir_ptr)) {
        fprintf(stderr, "error: could not ensure project build directory exists\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, state_ptr->project_install_dir_ptr)) {
        fprintf(stderr, "error: could not ensure project install directory exists\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, state_ptr->project_temp_dir_ptr)) {
        fprintf(stderr, "error: could not ensure project temp directory exists\n");
        return TCL_ERROR;
    }

    return TCL_OK;
}

int ttrek_WriteChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj *contents_ptr, int permissions) {
    Tcl_Channel chan = Tcl_OpenFileChannel(interp, Tcl_GetString(path_ptr), "w", permissions);
    Tcl_SetChannelOption(interp, chan, "-encoding", "utf-8");
    if (!chan) {
        fprintf(stderr, "error: could not open %s\n", Tcl_GetString(path_ptr));
        return TCL_ERROR;
    }
    Tcl_WriteObj(chan, contents_ptr);
    Tcl_Close(interp, chan);
    return TCL_OK;
}

int ttrek_WriteJsonFile(Tcl_Interp *interp, Tcl_Obj *path_ptr, cJSON *root) {
    Tcl_DString ds;
    Tcl_DStringInit(&ds);
    if (TCL_OK != tjson_TreeToJson(interp, root, 2, &ds)) {
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }
    Tcl_Obj *contents_ptr = Tcl_DStringToObj(&ds);
    Tcl_IncrRefCount(contents_ptr);
    Tcl_DStringFree(&ds);
    int result = ttrek_WriteChars(interp, path_ptr, contents_ptr, 0666);
    Tcl_DecrRefCount(contents_ptr);
    return result;
}

int ttrek_ReadChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj **contents_ptr) {
    Tcl_Channel chan = Tcl_OpenFileChannel(interp, Tcl_GetString(path_ptr), "r", 0666);
    Tcl_SetChannelOption(interp, chan, "-encoding", "utf-8");
    if (!chan) {
        fprintf(stderr, "error: could not open %s\n", Tcl_GetString(path_ptr));
        return TCL_ERROR;
    }
    Tcl_ReadChars(chan, *contents_ptr, -1, 0);
    Tcl_Close(interp, chan);
    return TCL_OK;
}

int ttrek_FileToJson(Tcl_Interp *interp, Tcl_Obj *path_ptr, cJSON **root) {
    Tcl_Obj *contents_ptr = Tcl_NewStringObj("", -1);
    Tcl_IncrRefCount(contents_ptr);
    if (TCL_OK != ttrek_ReadChars(interp, path_ptr, &contents_ptr)) {
        fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_ptr));
        Tcl_DecrRefCount(contents_ptr);
        return TCL_ERROR;
    }
    *root = cJSON_Parse(Tcl_GetString(contents_ptr));
    Tcl_DecrRefCount(contents_ptr);
    return TCL_OK;
}

int ttrek_ExecuteCommand(Tcl_Interp *interp, Tcl_Size argc, const char *argv[]) {
    void *handle;
    Tcl_Channel chan = Tcl_OpenCommandChannel(interp, argc, argv, TCL_STDOUT);
    if (!chan) {
        SetResult("could not open command channel");
        return TCL_ERROR;
    }
    Tcl_Obj *resultPtr = Tcl_NewStringObj("", -1);
    if (Tcl_GetChannelHandle(chan, TCL_READABLE, &handle) != TCL_OK) {
        SetResult("could not get channel handle");
        return TCL_ERROR;
    }
    // make "chan" non-blocking
    Tcl_SetChannelOption(interp, chan, "-blocking", "0");
    Tcl_SetChannelOption(interp, chan, "-buffering", "none");

    while (!Tcl_Eof(chan)) {
        if (Tcl_ReadChars(chan, resultPtr, -1, 0) < 0) {
            fprintf(stderr, "error reading from channel: %s\n", Tcl_GetString(Tcl_GetObjResult(interp)));
            SetResult("error reading from channel");
            return TCL_ERROR;
        }
        if (Tcl_GetCharLength(resultPtr) > 0) {
            fprintf(stderr, "%s", Tcl_GetString(resultPtr));
        }
    }

    int status = 0;
    waitpid(-1, &status, 0); // Replace -1 with the child process ID if known
    if (WIFEXITED(status)) {
        fprintf(stderr, "Exit status: %d\n", WEXITSTATUS(status));
        if (WEXITSTATUS(status)) {
            fprintf(stderr, "interp result: %s\n", Tcl_GetString(Tcl_GetObjResult(interp)));
            SetResult("command failed");
            return TCL_ERROR;
        }
    }

    Tcl_Close(interp, chan);
    return TCL_OK;
}

Tcl_Obj *ttrek_GetProjectDirForLocalMode(Tcl_Interp *interp) {
    Tcl_Obj *project_homedir_ptr = Tcl_FSGetCwd(interp);
    if (!project_homedir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return NULL;
    }
    Tcl_IncrRefCount(project_homedir_ptr);
    Tcl_Obj *project_filename_ptr = Tcl_NewStringObj(SPEC_JSON_FILE, -1);
    Tcl_IncrRefCount(project_filename_ptr);
    Tcl_Obj *path_project_file_ptr;
    while (Tcl_GetCharLength(project_homedir_ptr) > 0) {

        if (TCL_OK != ttrek_ResolvePath(interp, project_homedir_ptr, project_filename_ptr, &path_project_file_ptr)) {
            fprintf(stderr, "here0\n");
            Tcl_DecrRefCount(project_filename_ptr);
            Tcl_DecrRefCount(project_homedir_ptr);
            return NULL;
        }

        DBG(fprintf(stderr, "(candidate) path_project_file: %s\n", Tcl_GetString(path_project_file_ptr)));

        if (!path_project_file_ptr) {
            fprintf(stderr, "here1\n");
            Tcl_DecrRefCount(project_filename_ptr);
            Tcl_DecrRefCount(project_homedir_ptr);
            return NULL;
        }
        Tcl_IncrRefCount(path_project_file_ptr);

        if (ttrek_CheckFileExists(path_project_file_ptr) == TCL_OK) {
            Tcl_DecrRefCount(project_filename_ptr);
            Tcl_DecrRefCount(path_project_file_ptr);
            Tcl_IncrRefCount(project_homedir_ptr);
            return project_homedir_ptr;
        }
        Tcl_DecrRefCount(path_project_file_ptr);

        Tcl_Size list_length;
        Tcl_Obj *list_ptr = Tcl_FSSplitPath(project_homedir_ptr, &list_length);
        if (!list_ptr || !list_length) {
            return NULL;
        }
        DBG(fprintf(stderr, "GetProjectHomeDir: list_length: %zd list: %s\n", list_length, Tcl_GetString(list_ptr)));
        Tcl_DecrRefCount(project_homedir_ptr);
        Tcl_IncrRefCount(list_ptr);
        project_homedir_ptr = Tcl_FSJoinPath(list_ptr, list_length - 1);
        Tcl_IncrRefCount(project_homedir_ptr);
        Tcl_DecrRefCount(list_ptr);
    }
    Tcl_DecrRefCount(project_filename_ptr);
    Tcl_DecrRefCount(project_homedir_ptr);
    fprintf(stderr, "error: %s does not exist, run 'ttrek init' first\n", SPEC_JSON_FILE);
    return NULL;
}

Tcl_Obj *ttrek_GetProjectHomeDir(Tcl_Interp *interp, ttrek_mode_t mode) {
    if (mode == MODE_LOCAL) {
        return ttrek_GetProjectDirForLocalMode(interp);
    } else if (mode == MODE_USER) {
//        return ttrek_GetProjectDirForUserMode(interp);
    } else if (mode == MODE_GLOBAL) {
//        return ttrek_GetProjectDirForGlobalMode(interp);
    }
    return NULL;
}

Tcl_Obj *ttrek_GetProjectVenvDir(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr) {
    Tcl_Obj *venv_dir_ptr = Tcl_NewStringObj(VENV_DIR, -1);
    Tcl_IncrRefCount(venv_dir_ptr);
    Tcl_Obj *path_to_venv_dir_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_home_dir_ptr, venv_dir_ptr, &path_to_venv_dir_ptr)) {
        Tcl_DecrRefCount(venv_dir_ptr);
        return NULL;
    }
    Tcl_DecrRefCount(venv_dir_ptr);
    Tcl_IncrRefCount(path_to_venv_dir_ptr);
    return path_to_venv_dir_ptr;
}

Tcl_Obj *ttrek_GetVenvSubDir(Tcl_Interp *interp, Tcl_Obj *project_venv_dir_ptr, const char *subdir) {
    Tcl_Obj *install_dir_ptr = Tcl_NewStringObj(subdir, -1);
    Tcl_IncrRefCount(install_dir_ptr);
    Tcl_Obj *path_to_install_dir_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_venv_dir_ptr, install_dir_ptr, &path_to_install_dir_ptr)) {
        Tcl_DecrRefCount(install_dir_ptr);
        return NULL;
    }
    Tcl_DecrRefCount(install_dir_ptr);
    Tcl_IncrRefCount(path_to_install_dir_ptr);
    return path_to_install_dir_ptr;
}

Tcl_Obj *ttrek_GetSpecFilePath(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr) {
    Tcl_Obj *spec_filename_ptr = Tcl_NewStringObj(SPEC_JSON_FILE, -1);
    Tcl_IncrRefCount(spec_filename_ptr);
    Tcl_Obj *path_to_spec_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_home_dir_ptr, spec_filename_ptr, &path_to_spec_file_ptr)) {
        Tcl_DecrRefCount(spec_filename_ptr);
        return NULL;
    }
    Tcl_DecrRefCount(spec_filename_ptr);
    Tcl_IncrRefCount(path_to_spec_file_ptr);
    return path_to_spec_file_ptr;
}

cJSON *ttrek_GetSpecRoot(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr) {
    Tcl_Obj *path_to_spec_file_ptr = ttrek_GetSpecFilePath(interp, project_home_dir_ptr);
    cJSON *spec_root = NULL;
    if (TCL_OK != ttrek_FileToJson(interp, path_to_spec_file_ptr, &spec_root)) {
        fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_to_spec_file_ptr));
        Tcl_DecrRefCount(path_to_spec_file_ptr);
        return NULL;
    }
    Tcl_DecrRefCount(path_to_spec_file_ptr);
    return spec_root;
}

Tcl_Obj *ttrek_GetLockFilePath(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr) {
    Tcl_Obj *lock_filename_ptr = Tcl_NewStringObj(LOCK_JSON_FILE, -1);
    Tcl_IncrRefCount(lock_filename_ptr);
    Tcl_Obj *path_to_lock_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_home_dir_ptr, lock_filename_ptr, &path_to_lock_file_ptr)) {
        Tcl_DecrRefCount(lock_filename_ptr);
        return NULL;
    }
    Tcl_DecrRefCount(lock_filename_ptr);
    Tcl_IncrRefCount(path_to_lock_file_ptr);
    return path_to_lock_file_ptr;
}

cJSON *ttrek_GetLockRoot(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr) {

    Tcl_Obj *path_to_lock_file_ptr = ttrek_GetLockFilePath(interp, project_home_dir_ptr);

    cJSON *lock_root = NULL;
    if (TCL_OK != ttrek_FileToJson(interp, path_to_lock_file_ptr, &lock_root)) {
        fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_to_lock_file_ptr));
        Tcl_DecrRefCount(path_to_lock_file_ptr);
        Tcl_DecrRefCount(project_home_dir_ptr);
        return NULL;
    }

    Tcl_DecrRefCount(path_to_lock_file_ptr);
    Tcl_DecrRefCount(project_home_dir_ptr);

    return lock_root;
}

ttrek_state_t *ttrek_CreateState(Tcl_Interp *interp, int option_yes, int option_force, ttrek_mode_t mode, ttrek_strategy_t strategy) {
    ttrek_state_t *state_ptr = (ttrek_state_t *) Tcl_Alloc(sizeof(ttrek_state_t));
    if (!state_ptr) {
        return NULL;
    }
    Tcl_Obj *project_home_dir_ptr = ttrek_GetProjectHomeDir(interp, mode);
    if (!project_home_dir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return NULL;
    }

    DBG(fprintf(stderr, "project_home_dir: %s\n", Tcl_GetString(project_home_dir_ptr)));

    Tcl_Obj *path_to_spec_file_ptr = ttrek_GetSpecFilePath(interp, project_home_dir_ptr);

    if (TCL_OK != ttrek_CheckFileExists(path_to_spec_file_ptr)) {
        fprintf(stderr, "error: %s does not exist, run 'ttrek init' first\n", SPEC_JSON_FILE);
        Tcl_DecrRefCount(project_home_dir_ptr);
        Tcl_DecrRefCount(path_to_spec_file_ptr);
        return NULL;
    }

    Tcl_Obj *path_lock_file_ptr = ttrek_GetLockFilePath(interp, project_home_dir_ptr);

    if (TCL_OK != ttrek_CheckFileExists(path_lock_file_ptr)) {
        fprintf(stderr, "error: %s does not exist, run 'ttrek init' first\n", LOCK_JSON_FILE);
        Tcl_DecrRefCount(project_home_dir_ptr);
        Tcl_DecrRefCount(path_to_spec_file_ptr);
        Tcl_DecrRefCount(path_lock_file_ptr);
        return NULL;
    }

    Tcl_Obj *project_venv_dir_ptr = ttrek_GetProjectVenvDir(interp, project_home_dir_ptr);

    state_ptr->option_yes = option_yes;
    state_ptr->option_force = option_force;
    state_ptr->mode = mode;
    state_ptr->strategy = strategy;
    state_ptr->project_home_dir_ptr = project_home_dir_ptr;
    state_ptr->project_venv_dir_ptr = project_venv_dir_ptr;
    state_ptr->project_install_dir_ptr = ttrek_GetVenvSubDir(interp, project_venv_dir_ptr, INSTALL_DIR);
    state_ptr->project_build_dir_ptr = ttrek_GetVenvSubDir(interp, project_venv_dir_ptr, BUILD_DIR);
    state_ptr->project_temp_dir_ptr = ttrek_GetVenvSubDir(interp, project_venv_dir_ptr, TEMP_DIR);
    state_ptr->spec_json_path_ptr = path_to_spec_file_ptr;
    state_ptr->lock_json_path_ptr = path_lock_file_ptr;
    state_ptr->spec_root = ttrek_GetSpecRoot(interp, project_home_dir_ptr);
    state_ptr->lock_root = ttrek_GetLockRoot(interp, project_home_dir_ptr);

    // print all refCount for all dir_ptr in state_ptr
    DBG(fprintf(stderr, "project_home_dir_ptr refCount: %d\n", state_ptr->project_home_dir_ptr->refCount));
    DBG(fprintf(stderr, "project_venv_dir_ptr refCount: %d\n", state_ptr->project_venv_dir_ptr->refCount));
    DBG(fprintf(stderr, "project_install_dir_ptr refCount: %d\n", state_ptr->project_install_dir_ptr->refCount));
    DBG(fprintf(stderr, "project_build_dir_ptr refCount: %d\n", state_ptr->project_build_dir_ptr->refCount));
    DBG(fprintf(stderr, "project_temp_dir_ptr refCount: %d\n", state_ptr->project_temp_dir_ptr->refCount));
    DBG(fprintf(stderr, "spec_json_path_ptr refCount: %d\n", state_ptr->spec_json_path_ptr->refCount));
    DBG(fprintf(stderr, "lock_json_path_ptr refCount: %d\n", state_ptr->lock_json_path_ptr->refCount));

    if (TCL_OK != ttrek_EnsureSkeletonExists(interp, state_ptr)) {
        fprintf(stderr, "error: could not ensure skeleton exists\n");
        return NULL;
    }

    return state_ptr;
}

void ttrek_DestroyState(ttrek_state_t *state_ptr) {
    Tcl_DecrRefCount(state_ptr->project_home_dir_ptr);
    Tcl_DecrRefCount(state_ptr->project_venv_dir_ptr);
    Tcl_DecrRefCount(state_ptr->project_install_dir_ptr);
    Tcl_DecrRefCount(state_ptr->project_build_dir_ptr);
    Tcl_DecrRefCount(state_ptr->project_temp_dir_ptr);
    Tcl_DecrRefCount(state_ptr->spec_json_path_ptr);
    Tcl_DecrRefCount(state_ptr->lock_json_path_ptr);
    cJSON_Delete(state_ptr->spec_root);
    cJSON_Delete(state_ptr->lock_root);
    Tcl_Free((char *) state_ptr);
}

#define MAX_STRATEGY_LEN 7
ttrek_strategy_t ttrek_StrategyFromString(const char *strategy_str, ttrek_strategy_t default_strategy) {
    if (strategy_str == NULL) {
        return default_strategy;
    }

    if (strncmp(strategy_str, "latest", MAX_STRATEGY_LEN) == 0) {
        return STRATEGY_LATEST;
    } else if (strncmp(strategy_str, "favored", MAX_STRATEGY_LEN) == 0) {
        return STRATEGY_FAVORED;
    } else if (strncmp(strategy_str, "locked", MAX_STRATEGY_LEN) == 0) {
        return STRATEGY_LOCKED;
    }
    return STRATEGY_LATEST;
}


int ttrek_GetDirectDependencies(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *list_ptr) {
    cJSON *deps = cJSON_GetObjectItem(spec_root, "dependencies");
    if (!deps) {
        return TCL_ERROR;
    }

    cJSON *dep = NULL;
    for (int i = 0; i < cJSON_GetArraySize(deps); i++) {
        dep = cJSON_GetArrayItem(deps, i);
        char spec[256];
        snprintf(spec, 256, "%s@%s", dep->string, dep->valuestring);
        DBG(fprintf(stderr, "spec: %s\n", spec));
        if (TCL_OK != Tcl_ListObjAppendElement(interp, list_ptr, Tcl_NewStringObj(dep->string, -1))) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}


#define SP " "
#define NL "\n"
#define LBRACKET "["
#define RBRACKET "]"
#define LBRACE "{"
#define RBRACE "}"
#define COMMA ","

static int tjson_EscapeJsonString(Tcl_Obj *objPtr, Tcl_DString *dsPtr) {
    Tcl_Size length;
    const char *str = Tcl_GetStringFromObj(objPtr, &length);
    // loop through each character of the input string
    for (Tcl_Size i = 0; i < length; i++) {
        unsigned char c = str[i];
        switch (c) {
            case '"':
                Tcl_DStringAppend(dsPtr, "\\\"", 2);
                break;
            case '\\':
                Tcl_DStringAppend(dsPtr, "\\\\", 2);
                break;
            case '\b':
                Tcl_DStringAppend(dsPtr, "\\b", 2);
                break;
            case '\f':
                Tcl_DStringAppend(dsPtr, "\\f", 2);
                break;
            case '\n':
                Tcl_DStringAppend(dsPtr, "\\n", 2);
                break;
            case '\r':
                Tcl_DStringAppend(dsPtr, "\\r", 2);
                break;
            case '\t':
                Tcl_DStringAppend(dsPtr, "\\t", 2);
                break;
            default:
                if (c < 32) {
                    Tcl_DStringAppend(dsPtr, "\\u00", 4);
                    char hex[3];
                    sprintf(hex, "%02x", c);
                    Tcl_DStringAppend(dsPtr, hex, 2);
                } else {
                    char tempstr[2];
                    tempstr[0] = c;
                    tempstr[1] = '\0';
                    Tcl_DStringAppend(dsPtr, tempstr, 1);
                }
        }
    }
    return TCL_OK;
}

static int tjson_TreeToJson(Tcl_Interp *interp, cJSON *item, int num_spaces, Tcl_DString *dsPtr) {
    double d;
    switch ((item->type) & 0xFF)
    {
        case cJSON_NULL:
            Tcl_DStringAppend(dsPtr, "null", 4);
            return TCL_OK;
        case cJSON_False:
            Tcl_DStringAppend(dsPtr, "false", 5);
            return TCL_OK;
        case cJSON_True:
            Tcl_DStringAppend(dsPtr, "true", 4);
            return TCL_OK;
        case cJSON_Number:
            d = item->valuedouble;
            if (isnan(d) || isinf(d)) {
                Tcl_DStringAppend(dsPtr, "null", 4);
                return TCL_OK;
            } else if(d == (double)item->valueint) {
                Tcl_Size intstr_length;
                Tcl_Obj *intObjPtr = Tcl_NewIntObj(item->valueint);
                Tcl_IncrRefCount(intObjPtr);
                const char *intstr = Tcl_GetStringFromObj(intObjPtr, &intstr_length);
                Tcl_DStringAppend(dsPtr, intstr, intstr_length);
                Tcl_DecrRefCount(intObjPtr);
                return TCL_OK;
            } else {
                Tcl_Size doublestr_length;
                Tcl_Obj *doubleObjPtr = Tcl_NewDoubleObj(item->valuedouble);
                Tcl_IncrRefCount(doubleObjPtr);
                const char *doublestr = Tcl_GetStringFromObj(doubleObjPtr, &doublestr_length);
                Tcl_DStringAppend(dsPtr, doublestr, doublestr_length);
                Tcl_DecrRefCount(doubleObjPtr);
                return TCL_OK;
            }
        case cJSON_Raw:
        {
            if (item->valuestring == NULL)
            {
                Tcl_DStringAppend(dsPtr, "\"\"", 2);
                return TCL_OK;
            }
            Tcl_DStringAppend(dsPtr, "\"", 1);
            Tcl_Obj *strObjPtr = Tcl_NewStringObj(item->valuestring, -1);
            Tcl_IncrRefCount(strObjPtr);
            tjson_EscapeJsonString(strObjPtr, dsPtr);
            Tcl_DStringAppend(dsPtr, "\"", 1);
            Tcl_DecrRefCount(strObjPtr);
            return TCL_OK;
        }

        case cJSON_String:
            Tcl_DStringAppend(dsPtr, "\"", 1);
            Tcl_Obj *strObjPtr = Tcl_NewStringObj(item->valuestring, -1);
            Tcl_IncrRefCount(strObjPtr);
            tjson_EscapeJsonString(strObjPtr, dsPtr);
            Tcl_DStringAppend(dsPtr, "\"", 1);
            Tcl_DecrRefCount(strObjPtr);
            return TCL_OK;
        case cJSON_Array:
            Tcl_DStringAppend(dsPtr, LBRACKET, 1);
            if (num_spaces) {
                Tcl_DStringAppend(dsPtr, NL, 1);
            }
            cJSON *current_element = item->child;
            int first_array_element = 1;
            while (current_element != NULL) {
                if (first_array_element) {
                    first_array_element = 0;
                } else {
                    Tcl_DStringAppend(dsPtr, COMMA, 1);
                    if (num_spaces) {
                        Tcl_DStringAppend(dsPtr, NL, 1);
                    }
                }
                if (num_spaces) {
                    for (int i = 0; i < num_spaces; i++) {
                        Tcl_DStringAppend(dsPtr, SP, 1);
                    }
                }
                if (TCL_OK != tjson_TreeToJson(interp, current_element, num_spaces > 0 ? num_spaces + 2 : 0, dsPtr)) {
                    return TCL_ERROR;
                }
                current_element = current_element->next;
            }
            if (num_spaces) {
                Tcl_DStringAppend(dsPtr, NL, 1);
                for (int i = 0; i < num_spaces - 2; i++) {
                    Tcl_DStringAppend(dsPtr, SP, 1);
                }
            }
            Tcl_DStringAppend(dsPtr, RBRACKET, 1);
            return TCL_OK;
        case cJSON_Object:
            Tcl_DStringAppend(dsPtr, LBRACE, 1);
            if (num_spaces) {
                Tcl_DStringAppend(dsPtr, NL, 1);
            }
            cJSON *current_item = item->child;
            int first_object_item = 1;
            while (current_item) {
                if (first_object_item) {
                    first_object_item = 0;
                } else {
                    Tcl_DStringAppend(dsPtr, COMMA, 1);
                    if (num_spaces) {
                        Tcl_DStringAppend(dsPtr, NL, 1);
                    }
                }
                if (num_spaces) {
                    for (int i = 0; i < num_spaces; i++) {
                        Tcl_DStringAppend(dsPtr, SP, 1);
                    }
                }
                Tcl_DStringAppend(dsPtr, "\"", 1);
                Tcl_Obj *strToEscapeObjPtr = Tcl_NewStringObj(current_item->string, -1);
                Tcl_IncrRefCount(strToEscapeObjPtr);
                tjson_EscapeJsonString(strToEscapeObjPtr, dsPtr);
                Tcl_DecrRefCount(strToEscapeObjPtr);
                Tcl_DStringAppend(dsPtr, "\":", 2);
                if (num_spaces) {
                    Tcl_DStringAppend(dsPtr, SP, 1);
                }
                if (TCL_OK != tjson_TreeToJson(interp, current_item, num_spaces > 0 ? num_spaces + 2 : 0, dsPtr)) {
                    return TCL_ERROR;
                }
                current_item = current_item->next;
            }
            if (num_spaces) {
                Tcl_DStringAppend(dsPtr, NL, 1);
                for (int i = 0; i < num_spaces - 2; i++) {
                    Tcl_DStringAppend(dsPtr, SP, 1);
                }
            }
            Tcl_DStringAppend(dsPtr, RBRACE, 1);
            return TCL_OK;
        default:
            Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid type", -1));
            return TCL_ERROR;
    }
}
