/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include <ctype.h>
#include "ttrek_useflags.h"

int ttrek_IsValidUseFlagName(const char *name) {
    return name && name[0] && name[0] != '-' && strspn(name,
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") == strlen(name);
}

int ttrek_IsValidUseFlag(const char *use_flag_str) {
    return use_flag_str && (use_flag_str[0] == '+' || use_flag_str[0] == '-') &&
        ttrek_IsValidUseFlagName(use_flag_str + 1);
}

static int ttrek_FlagArrayToList(Tcl_Interp *interp, const cJSON *flags,
                                const char *field, Tcl_Obj *list_ptr) {
    if (!cJSON_IsArray(flags)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s must be an array of USE flag strings", field));
        return TCL_ERROR;
    }
    const cJSON *flag;
    cJSON_ArrayForEach(flag, flags) {
        if (!cJSON_IsString(flag) || !(strcmp(field, "iuse") == 0
                ? ttrek_IsValidUseFlagName(flag->valuestring) : ttrek_IsValidUseFlag(flag->valuestring))) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Invalid USE flag in %s", field));
            return TCL_ERROR;
        }
        if (Tcl_ListObjAppendElement(interp, list_ptr, Tcl_NewStringObj(flag->valuestring, -1)) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

int ttrek_GetUseFlags(Tcl_Interp *interp, cJSON *manifest_root, Tcl_Obj *list_ptr) {
    if (!cJSON_IsObject(manifest_root)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid installed manifest JSON", -1));
        return TCL_ERROR;
    }
    cJSON *flags = cJSON_GetObjectItemCaseSensitive(manifest_root, "useFlags");
    if (!flags) { return TCL_OK; }
    int status = ttrek_FlagArrayToList(interp, flags, "useFlags", list_ptr);
    if (status == TCL_OK) {
        Tcl_HashTable ht;
        Tcl_InitHashTable(&ht, TCL_STRING_KEYS);
        status = ttrek_PopulateHashTableFromUseFlagsList(interp, list_ptr, &ht);
        Tcl_DeleteHashTable(&ht);
    }
    return status;
}


int ttrek_PopulateHashTableFromUseFlagsList(Tcl_Interp *interp, Tcl_Obj *list_ptr, Tcl_HashTable *ht) {

    Tcl_Size list_len;
    if (TCL_OK != Tcl_ListObjLength(interp, list_ptr, &list_len)) {
        return TCL_ERROR;
    }

    for (Tcl_Size i = 0; i < list_len; i++) {
        Tcl_Obj *elem;
        if (TCL_OK != Tcl_ListObjIndex(interp, list_ptr, i, &elem)) {
            return TCL_ERROR;
        }
        const char *use_flag_str = Tcl_GetString(elem);
        if (!ttrek_IsValidUseFlag(use_flag_str)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Invalid USE flag: %s", use_flag_str));
            return TCL_ERROR;
        }
        int polarity = 0;
        if (use_flag_str[0] == '+') {
            polarity = 1;
        } else if (use_flag_str[0] == '-') {
            polarity = 0;
        } else {
            return TCL_ERROR;
        }
        const char *use_flag_name = use_flag_str + 1;

        int newEntry = 0;
        Tcl_HashEntry *entry = Tcl_CreateHashEntry(ht, use_flag_name, &newEntry);
        if (!newEntry && PTR2INT(Tcl_GetHashValue(entry)) != polarity) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Conflicting USE flags for %s", use_flag_name));
            return TCL_ERROR;
        }
        Tcl_SetHashValue(entry, INT2PTR(polarity));
    }

    return TCL_OK;
}

int ttrek_PopulateUseFlagsListFromHashTable(Tcl_Interp *interp, Tcl_HashTable *ht, Tcl_Obj *list_ptr) {

    Tcl_HashSearch search;
    Tcl_HashEntry *entry = Tcl_FirstHashEntry(ht, &search);
    while (entry) {
        const char *use_flag_name = Tcl_GetHashKey(ht, entry);
        int polarity = PTR2INT(Tcl_GetHashValue(entry));
        Tcl_Obj *use_flag = Tcl_ObjPrintf("%c%s", polarity ? '+' : '-', use_flag_name);
        if (TCL_OK != Tcl_ListObjAppendElement(interp, list_ptr, use_flag)) {
            return TCL_ERROR;
        }
        entry = Tcl_NextHashEntry(&search);
    }

    return TCL_OK;
}

int ttrek_PopulateIUseFlagsListFromNode(Tcl_Interp *interp, cJSON *use_node, Tcl_Obj *list_ptr) {
    return ttrek_FlagArrayToList(interp, use_node, "iuse", list_ptr);
}

int ttrek_SetUseFlags(Tcl_Interp *interp, cJSON *manifest_root, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (!cJSON_IsObject(manifest_root)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid installed manifest JSON", -1));
        return TCL_ERROR;
    }
    Tcl_Obj *list = Tcl_NewListObj(objc, objv);
    Tcl_IncrRefCount(list);
    Tcl_HashTable ht;
    Tcl_InitHashTable(&ht, TCL_STRING_KEYS);
    int status = ttrek_PopulateHashTableFromUseFlagsList(interp, list, &ht);
    Tcl_DecrRefCount(list);
    if (status == TCL_OK) {
        cJSON *flags = cJSON_CreateArray();
        for (Tcl_Size i = 0; i < objc; ++i) {
            const char *flag = Tcl_GetString(objv[i]);
            Tcl_HashEntry *entry = Tcl_FindHashEntry(&ht, flag + 1);
            if (entry) {
                cJSON_AddItemToArray(flags, cJSON_CreateString(flag));
                Tcl_DeleteHashEntry(entry);
            }
        }
        cJSON_DeleteItemFromObjectCaseSensitive(manifest_root, "useFlags");
        cJSON_AddItemToObject(manifest_root, "useFlags", flags);
    }
    Tcl_DeleteHashTable(&ht);
    return status;
}

int ttrek_AddUseFlags(Tcl_Interp *interp, cJSON *manifest_root, Tcl_Size objc, Tcl_Obj *const objv[]) {

    Tcl_Obj *list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(list_ptr);
    if (TCL_OK != ttrek_GetUseFlags(interp, manifest_root, list_ptr)) {
        Tcl_DecrRefCount(list_ptr);
        return TCL_ERROR;
    }

    Tcl_HashTable ht;
    Tcl_InitHashTable(&ht, TCL_STRING_KEYS);
    if (TCL_OK != ttrek_PopulateHashTableFromUseFlagsList(interp, list_ptr, &ht)) {
        Tcl_DecrRefCount(list_ptr);
        Tcl_DeleteHashTable(&ht);
        return TCL_ERROR;
    }

    for (Tcl_Size i = 0; i < objc; i++) {
        Tcl_Obj *use_flag = objv[i];
        const char *use_flag_str = Tcl_GetString(use_flag);
        if (!ttrek_IsValidUseFlag(use_flag_str)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Invalid USE flag: %s", use_flag_str));
            Tcl_DecrRefCount(list_ptr);
            Tcl_DeleteHashTable(&ht);
            return TCL_ERROR;
        }
        int polarity = 0;
        if (use_flag_str[0] == '+') {
            polarity = 1;
        } else if (use_flag_str[0] == '-') {
            polarity = 0;
        } else {
            Tcl_DecrRefCount(list_ptr);
            Tcl_DeleteHashTable(&ht);
            return TCL_ERROR;
        }
        const char *use_flag_name = use_flag_str + 1;

        int newEntry = 0;
        Tcl_HashEntry *entry = Tcl_CreateHashEntry(&ht, use_flag_name, &newEntry);
        Tcl_SetHashValue(entry, INT2PTR(polarity));
    }

    Tcl_Obj *out_list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(out_list_ptr);
    if (TCL_OK != ttrek_PopulateUseFlagsListFromHashTable(interp, &ht, out_list_ptr)) {
        Tcl_DecrRefCount(list_ptr);
        Tcl_DecrRefCount(out_list_ptr);
        Tcl_DeleteHashTable(&ht);
        return TCL_ERROR;
    }

    Tcl_Size addObjc;
    Tcl_Obj **addObjv;
    if (TCL_OK != Tcl_ListObjGetElements(interp, out_list_ptr, &addObjc, &addObjv)) {
        Tcl_DecrRefCount(list_ptr);
        Tcl_DecrRefCount(out_list_ptr);
        Tcl_DeleteHashTable(&ht);
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_SetUseFlags(interp, manifest_root, addObjc, addObjv)) {
        Tcl_DecrRefCount(list_ptr);
        Tcl_DecrRefCount(out_list_ptr);
        Tcl_DeleteHashTable(&ht);
        return TCL_ERROR;
    }

    Tcl_DeleteHashTable(&ht);
    Tcl_DecrRefCount(list_ptr);
    Tcl_DecrRefCount(out_list_ptr);
    return TCL_OK;
}

int ttrek_DelUseFlags(Tcl_Interp *interp, cJSON *manifest_root, Tcl_Size objc, Tcl_Obj *const objv[]) {

    Tcl_Obj *list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(list_ptr);
    if (TCL_OK != ttrek_GetUseFlags(interp, manifest_root, list_ptr)) {
        Tcl_DecrRefCount(list_ptr);
        return TCL_ERROR;
    }

    Tcl_HashTable ht;
    Tcl_InitHashTable(&ht, TCL_STRING_KEYS);
    if (TCL_OK != ttrek_PopulateHashTableFromUseFlagsList(interp, list_ptr, &ht)) {
        Tcl_DecrRefCount(list_ptr);
        Tcl_DeleteHashTable(&ht);
        return TCL_ERROR;
    }

    for (Tcl_Size i = 0; i < objc; i++) {
        Tcl_Obj *use_flag = objv[i];
        const char *use_flag_str = Tcl_GetString(use_flag);
        if (!ttrek_IsValidUseFlag(use_flag_str)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Invalid USE flag: %s", use_flag_str));
            Tcl_DecrRefCount(list_ptr);
            Tcl_DeleteHashTable(&ht);
            return TCL_ERROR;
        }
        const char *use_flag_name = use_flag_str + 1;

        Tcl_HashEntry *entry = Tcl_FindHashEntry(&ht, use_flag_name);
        if (entry) {
            Tcl_DeleteHashEntry(entry);
        }
    }

    Tcl_Obj *out_list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(out_list_ptr);
    if (TCL_OK != ttrek_PopulateUseFlagsListFromHashTable(interp, &ht, out_list_ptr)) {
        Tcl_DecrRefCount(list_ptr);
        Tcl_DecrRefCount(out_list_ptr);
        Tcl_DeleteHashTable(&ht);
        return TCL_ERROR;
    }

    Tcl_Size delObjc;
    Tcl_Obj **delObjv;
    if (TCL_OK != Tcl_ListObjGetElements(interp, out_list_ptr, &delObjc, &delObjv)) {
        Tcl_DecrRefCount(list_ptr);
        Tcl_DecrRefCount(out_list_ptr);
        Tcl_DeleteHashTable(&ht);
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_SetUseFlags(interp, manifest_root, delObjc, delObjv)) {
        Tcl_DecrRefCount(list_ptr);
        Tcl_DecrRefCount(out_list_ptr);
        Tcl_DeleteHashTable(&ht);
        return TCL_ERROR;
    }

    Tcl_DeleteHashTable(&ht);
    Tcl_DecrRefCount(list_ptr);
    Tcl_DecrRefCount(out_list_ptr);
    return TCL_OK;
}

int ttrek_HashTableContainsUseFlag(Tcl_Interp *interp, Tcl_HashTable *ht, const char *use_flag_str, int *contains_p) {

    *contains_p = 0;
    if (!ttrek_IsValidUseFlag(use_flag_str)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("Invalid USE flag: %s", use_flag_str ? use_flag_str : ""));
        return TCL_ERROR;
    }

    int polarity = 0;
    if (use_flag_str[0] == '+') {
        polarity = 1;
    } else if (use_flag_str[0] == '-') {
        polarity = 0;
    } else {
        return TCL_ERROR;
    }
    const char *use_flag_name = use_flag_str + 1;

    Tcl_HashEntry *entry = Tcl_FindHashEntry(ht, use_flag_name);
    int entry_polarity = entry ? PTR2INT(Tcl_GetHashValue(entry)) : 0;
    *contains_p = entry_polarity == polarity;

    return TCL_OK;
}

int ttrek_HashTableIntersectionWithIUse(Tcl_Interp *interp, Tcl_HashTable *ht, Tcl_Obj *iuse_list_ptr, Tcl_Obj *result_list_ptr) {

    Tcl_Size iuse_list_len;
    if (TCL_OK != Tcl_ListObjLength(interp, iuse_list_ptr, &iuse_list_len)) {
        return TCL_ERROR;
    }

    for (Tcl_Size i = 0; i < iuse_list_len; i++) {
        Tcl_Obj *elem;
        if (TCL_OK != Tcl_ListObjIndex(interp, iuse_list_ptr, i, &elem)) {
            return TCL_ERROR;
        }
        const char *name = Tcl_GetString(elem);
        if (!ttrek_IsValidUseFlagName(name)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Invalid unsigned USE flag name in iuse: %s", name));
            return TCL_ERROR;
        }
        Tcl_HashEntry *entry = Tcl_FindHashEntry(ht, name);
        int enabled = entry && PTR2INT(Tcl_GetHashValue(entry));
        if (TCL_OK != Tcl_ListObjAppendElement(interp, result_list_ptr,
                Tcl_ObjPrintf("%c%s", enabled ? '+' : '-', name))) {
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}

int ttrek_EvaluateUseCondition(Tcl_Interp *interp, Tcl_HashTable *flags,
                              const cJSON *condition, int *enabled) {
    *enabled = 1;
    if (!condition) { return TCL_OK; }
    if (!cJSON_IsString(condition)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("'if' must be a USE flag string", -1));
        return TCL_ERROR;
    }
    const char *cursor = condition->valuestring;
    int any = 0;
    while (*cursor) {
        while (isspace((unsigned char)*cursor)) { ++cursor; }
        if (!*cursor) { break; }
        const char *start = cursor;
        while (*cursor && !isspace((unsigned char)*cursor)) { ++cursor; }
        Tcl_Obj *token = Tcl_NewStringObj(start, cursor - start);
        Tcl_IncrRefCount(token);
        int matches = 0;
        int status = ttrek_HashTableContainsUseFlag(interp, flags, Tcl_GetString(token), &matches);
        Tcl_DecrRefCount(token);
        if (status != TCL_OK) { return status; }
        *enabled = *enabled && matches;
        any = 1;
    }
    if (!any) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("'if' must contain a USE flag", -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/* A NULL version denotes a valid dependency excluded by its condition. */
int ttrek_GetDependencyVersion(Tcl_Interp *interp, Tcl_HashTable *flags,
                               const cJSON *dependency, const char **version) {
    const cJSON *range = cJSON_IsObject(dependency)
        ? cJSON_GetObjectItemCaseSensitive(dependency, "version") : dependency;
    *version = NULL;
    if (!cJSON_IsString(range)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Dependency must be a version string or an object with a version string", -1));
        return TCL_ERROR;
    }
    int enabled;
    if (ttrek_EvaluateUseCondition(interp, flags,
            cJSON_GetObjectItemCaseSensitive(dependency, "if"), &enabled) != TCL_OK) { return TCL_ERROR; }
    if (enabled) { *version = range->valuestring; }
    return TCL_OK;
}
