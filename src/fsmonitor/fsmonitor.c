/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "fsmonitor.h"
#include <stdio.h>
#include <string.h>

int ttrek_DiffList(Tcl_Interp *interp, Tcl_Obj *list1, Tcl_Obj *list2, Tcl_Obj *diff, Tcl_Size ncut) {

    Tcl_HashTable ht;
    Tcl_InitHashTable(&ht, TCL_STRING_KEYS);

    // add all elements of list1 to the hash table
    Tcl_Size list1_len;
    if (TCL_OK != Tcl_ListObjLength(interp, list1, &list1_len)) {
        fprintf(stderr, "error: could not get length of list1\n");
        return TCL_ERROR;
    }

    for (Tcl_Size i = 0; i < list1_len; i++) {
        Tcl_Obj *elem;
        if (TCL_OK != Tcl_ListObjIndex(interp, list1, i, &elem)) {
            fprintf(stderr, "error: could not get element %" TCL_SIZE_MODIFIER "d"
                " of list1\n", i);
            return TCL_ERROR;
        }
        Tcl_Size elem_len;
        const char *elem_str = Tcl_GetStringFromObj(elem, &elem_len);
        if (elem_len < ncut) {
            fprintf(stderr, "error: element %" TCL_SIZE_MODIFIER "d of list2 is"
                " shorter than ncut\n", i);
            return TCL_ERROR;
        }
        int dummyNewEntry;
        Tcl_CreateHashEntry(&ht, &elem_str[ncut], &dummyNewEntry);
    }

    // remove all elements of list2 from the hash table
    Tcl_Size list2_len;
    if (TCL_OK != Tcl_ListObjLength(interp, list2, &list2_len)) {
        fprintf(stderr, "error: could not get length of list2\n");
        return TCL_ERROR;
    }

    for (Tcl_Size i = 0; i < list2_len; i++) {
        Tcl_Obj *elem;
        if (TCL_OK != Tcl_ListObjIndex(interp, list2, i, &elem)) {
            fprintf(stderr, "error: could not get element %" TCL_SIZE_MODIFIER "d"
                " of list2\n", i);
            return TCL_ERROR;
        }
        Tcl_Size elem_len;
        const char *elem_str = Tcl_GetStringFromObj(elem, &elem_len);
        if (elem_len < ncut) {
            fprintf(stderr, "error: element %" TCL_SIZE_MODIFIER "d of list2 is"
                " shorter than ncut\n", i);
            return TCL_ERROR;
        }
        Tcl_HashEntry *entry = Tcl_FindHashEntry(&ht, &elem_str[ncut]);
        if (entry) {
            Tcl_DeleteHashEntry(entry);
        }
    }

    // add all remaining elements of the hash table to the diff list
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    for (entry = Tcl_FirstHashEntry(&ht, &search); entry != NULL; entry = Tcl_NextHashEntry(&search)) {
        const char *key = Tcl_GetHashKey(&ht, entry);
        Tcl_ListObjAppendElement(interp, diff, Tcl_NewStringObj(key, -1));
    }

    Tcl_DeleteHashTable(&ht);
    return TCL_OK;

}

int ttrek_MatchInDirectory(Tcl_Interp *interp, Tcl_Obj *result_ptr, Tcl_Obj *path_ptr, const char *pattern, int recursive, Tcl_GlobTypeData *types) {
    // get the list of files in the directory
    Tcl_Obj *files_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(files_ptr);

    if (TCL_OK != Tcl_FSMatchInDirectory(interp, files_ptr, path_ptr, pattern, types)) {
        fprintf(stderr, "error: could not list files in %s\n", Tcl_GetString(path_ptr));
        Tcl_DecrRefCount(files_ptr);
        return TCL_ERROR;
    }

    // append the list of files to the result
    Tcl_Size list_len;
    if (TCL_OK != Tcl_ListObjLength(interp, files_ptr, &list_len)) {
        fprintf(stderr, "error: could not get length of files\n");
        Tcl_DecrRefCount(files_ptr);
        return TCL_ERROR;
    }

    for (Tcl_Size i = 0; i < list_len; i++) {
        Tcl_Obj *elem;
        if (TCL_OK != Tcl_ListObjIndex(interp, files_ptr, i, &elem)) {
            fprintf(stderr, "error: could not get element %" TCL_SIZE_MODIFIER "d of files\n", i);
            Tcl_DecrRefCount(files_ptr);
            return TCL_ERROR;
        }
        Tcl_ListObjAppendElement(interp, result_ptr, elem);
    }
    Tcl_DecrRefCount(files_ptr);

    // if recursive, get the list of directories in the directory
    if (recursive) {

        Tcl_Obj *dirs_ptr = Tcl_NewListObj(0, NULL);
        Tcl_IncrRefCount(dirs_ptr);

        Tcl_GlobTypeData dirtypes;
        dirtypes.type = TCL_GLOB_TYPE_DIR;
        dirtypes.perm = 0;
        dirtypes.macCreator = NULL;
        dirtypes.macType = NULL;
        if (TCL_OK != Tcl_FSMatchInDirectory(interp, dirs_ptr, path_ptr, "*", &dirtypes)) {
            fprintf(stderr, "error: could not list directories in %s\n", Tcl_GetString(path_ptr));
            Tcl_DecrRefCount(dirs_ptr);
            return TCL_ERROR;
        }

        // for each directory, call MatchInDirectory
        Tcl_Size dirs_len;
        if (TCL_OK != Tcl_ListObjLength(interp, dirs_ptr, &dirs_len)) {
            fprintf(stderr, "error: could not get length of dirs\n");
            Tcl_DecrRefCount(dirs_ptr);
            return TCL_ERROR;
        }

        for (Tcl_Size i = 0; i < dirs_len; i++) {
            Tcl_Obj *dir;
            if (TCL_OK != Tcl_ListObjIndex(interp, dirs_ptr, i, &dir)) {
                fprintf(stderr, "error: could not get element %" TCL_SIZE_MODIFIER "d of dirs\n", i);
                Tcl_DecrRefCount(dirs_ptr);
                return TCL_ERROR;
            }
            if (TCL_OK != ttrek_MatchInDirectory(interp, result_ptr, dir, pattern, recursive, types)) {
                fprintf(stderr, "error: could not match in directory %s\n", Tcl_GetString(dir));
                Tcl_DecrRefCount(dirs_ptr);
                return TCL_ERROR;
            }
        }
    }
    return TCL_OK;
}

int ttrek_FSMonitor_AddWatch(Tcl_Interp *interp, Tcl_Obj *project_install_dir_ptr, ttrek_fsmonitor_state_t *state_ptr) {
    // list all files in the project_install_dir_ptr

    state_ptr->files_before = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(state_ptr->files_before);

    Tcl_GlobTypeData filetypes;
    filetypes.type = TCL_GLOB_TYPE_FILE;
    filetypes.perm = 0;
    filetypes.macCreator = NULL;
    filetypes.macType = NULL;
    if (TCL_OK != ttrek_MatchInDirectory(interp, state_ptr->files_before, project_install_dir_ptr, "*", 1, &filetypes)) {
        fprintf(stderr, "error: could not list files in %s\n", Tcl_GetString(project_install_dir_ptr));
        Tcl_DecrRefCount(state_ptr->files_before);
        return TCL_ERROR;
    }

    return TCL_OK;
}

int ttrek_FSMonitor_ReadChanges(Tcl_Interp *interp, Tcl_Obj *project_install_dir_ptr, ttrek_fsmonitor_state_t *state_ptr) {
    Tcl_Obj *files_after = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(files_after);

    Tcl_GlobTypeData filetypes;
    filetypes.type = TCL_GLOB_TYPE_FILE;
    filetypes.perm = 0;
    filetypes.macCreator = NULL;
    filetypes.macType = NULL;
    if (TCL_OK != ttrek_MatchInDirectory(interp, files_after, project_install_dir_ptr, "*", 1, &filetypes)) {
        fprintf(stderr, "error: could not list files in %s\n", Tcl_GetString(project_install_dir_ptr));
        Tcl_DecrRefCount(files_after);
        return TCL_ERROR;
    }
    state_ptr->files_diff = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(state_ptr->files_diff);

    Tcl_Size project_install_dir_len;
    // Here we just need to get the length of project_install_dir_ptr in
    // project_install_dir_len, so we can ignore the result of the function.
    Tcl_GetStringFromObj(project_install_dir_ptr, &project_install_dir_len);

    // project_install_dir_len + 1 to cut the slash in the beginning of the path for file in the diff list
    if (TCL_OK != ttrek_DiffList(interp, files_after, state_ptr->files_before, state_ptr->files_diff, project_install_dir_len + 1)) {
        fprintf(stderr, "error: could not diff files\n");
        Tcl_DecrRefCount(files_after);
        Tcl_DecrRefCount(state_ptr->files_diff);
        return TCL_ERROR;
    }

    Tcl_DecrRefCount(files_after);
    return TCL_OK;
}

int ttrek_FSMonitor_RemoveWatch(Tcl_Interp *interp, ttrek_fsmonitor_state_t *state_ptr) {
    UNUSED(interp);
    Tcl_DecrRefCount(state_ptr->files_before);
    if (state_ptr->files_diff) {
        Tcl_DecrRefCount(state_ptr->files_diff);
    }
    return TCL_OK;
}
