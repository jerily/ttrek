/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <tcl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "common.h"
#include "cjson/cJSON.h"

int ttrek_ResolvePath(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr, Tcl_Obj *filename_ptr, Tcl_Obj **path_ptr) {
    Tcl_Obj *objv[1] = {filename_ptr};
    *path_ptr = Tcl_FSJoinToPath(project_home_dir_ptr, 1, objv);

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

int ttrek_WriteChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj *contents_ptr, int permissions) {
    Tcl_Channel chan = Tcl_OpenFileChannel(interp, Tcl_GetString(path_ptr), "w", permissions);
    if (!chan) {
        fprintf(stderr, "error: could not open %s\n", Tcl_GetString(path_ptr));
        return TCL_ERROR;
    }
    Tcl_WriteObj(chan, contents_ptr);
    Tcl_Close(interp, chan);
    return TCL_OK;
}

int ttrek_WriteJsonFile(Tcl_Interp *interp, Tcl_Obj *path_ptr, cJSON *root) {
    char buffer[65536];
    cJSON_PrintPreallocated(root, buffer, sizeof(buffer), 1);
    return ttrek_WriteChars(interp, path_ptr, Tcl_NewStringObj(buffer, -1), 0666);
}

int ttrek_ReadChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj **contents_ptr) {
    Tcl_Channel chan = Tcl_OpenFileChannel(interp, Tcl_GetString(path_ptr), "r", 0666);
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
    Tcl_Channel chan = Tcl_OpenCommandChannel(interp, argc, argv, TCL_STDOUT|TCL_STDERR);
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

Tcl_Obj *ttrek_GetProjectHomeDir(Tcl_Interp *interp) {
    Tcl_Obj *project_homedir_ptr = Tcl_FSGetCwd(interp);
    if (!project_homedir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return NULL;
    }
    Tcl_IncrRefCount(project_homedir_ptr);
    Tcl_Obj *project_filename_ptr = Tcl_NewStringObj(PACKAGES_JSON_FILE, -1);
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
            Tcl_DecrRefCount(project_homedir_ptr);
            Tcl_DecrRefCount(path_project_file_ptr);
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
    fprintf(stderr, "error: %s does not exist, run 'ttrek init' first\n", PACKAGES_JSON_FILE);
    return NULL;
}