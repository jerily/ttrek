/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "ttrek_useflags.h"

#define MAX_USE_FLAG_LEN 128
int ttrek_IsValidUseFlag(const char *use_flag_str) {
    if (use_flag_str == NULL) {
        return 0;
    }

    if (strnlen(use_flag_str, MAX_USE_FLAG_LEN) < 2) {
        return 0;
    }

    if (use_flag_str[0] == '+' || use_flag_str[0] == '-') {
        return 1;
    }
    return 0;
}

int ttrek_GetUseFlags(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *list_ptr) {

    if (!cJSON_HasObjectItem(spec_root, "useFlags")) {
        return TCL_OK;
    }

    cJSON *use = cJSON_GetObjectItem(spec_root, "useFlags");

    for (int i = 0; i < cJSON_GetArraySize(use); i++) {
        cJSON *use_flag_node = cJSON_GetArrayItem(use, i);
        Tcl_Obj *use_flag = Tcl_NewStringObj(use_flag_node->valuestring, -1);
        if (TCL_OK != Tcl_ListObjAppendElement(interp, list_ptr, use_flag)) {
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}

int ttrek_SetUseFlags(Tcl_Interp *interp, cJSON *spec_root, Tcl_Size objc, Tcl_Obj *const objv[]) {

    UNUSED(interp);

    cJSON *use_node = cJSON_CreateArray();

    for (Tcl_Size i = 0; i < objc; i++) {
        cJSON_AddItemToArray(use_node, cJSON_CreateString(Tcl_GetString(objv[i])));
    }

    if (!cJSON_HasObjectItem(spec_root, "useFlags")) {
        cJSON_AddItemToObject(spec_root, "useFlags", use_node);
    } else {
        cJSON_ReplaceItemInObject(spec_root, "useFlags", use_node);
    }

    return TCL_OK;
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
        char use_flag_str[128];
        if (polarity) {
            snprintf(use_flag_str, 128, "+%s", use_flag_name);
        } else {
            snprintf(use_flag_str, 128, "-%s", use_flag_name);
        }
        Tcl_Obj *use_flag = Tcl_NewStringObj(use_flag_str, -1);
        if (TCL_OK != Tcl_ListObjAppendElement(interp, list_ptr, use_flag)) {
            return TCL_ERROR;
        }
        entry = Tcl_NextHashEntry(&search);
    }

    return TCL_OK;
}

int ttrek_PopulateIUseFlagsListFromNode(Tcl_Interp *interp, cJSON *use_node, Tcl_Obj *list_ptr) {
    for (int i = 0; i < cJSON_GetArraySize(use_node); i++) {
        cJSON *use_flag_node = cJSON_GetArrayItem(use_node, i);
        Tcl_Obj *use_flag = Tcl_NewStringObj(use_flag_node->valuestring, -1);
        if (TCL_OK != Tcl_ListObjAppendElement(interp, list_ptr, use_flag)) {
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}

int ttrek_AddUseFlags(Tcl_Interp *interp, cJSON *spec_root, Tcl_Size objc, Tcl_Obj *const objv[]) {

    Tcl_Obj *list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(list_ptr);
    if (TCL_OK != ttrek_GetUseFlags(interp, spec_root, list_ptr)) {
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

    if (TCL_OK != ttrek_SetUseFlags(interp, spec_root, addObjc, addObjv)) {
        Tcl_DecrRefCount(list_ptr);
        Tcl_DecrRefCount(out_list_ptr);
        Tcl_DeleteHashTable(&ht);
        return TCL_ERROR;
    }

    Tcl_DeleteHashTable(&ht);
    Tcl_DecrRefCount(list_ptr);
    return TCL_OK;
}

int ttrek_DelUseFlags(Tcl_Interp *interp, cJSON *spec_root, Tcl_Size objc, Tcl_Obj *const objv[]) {

    Tcl_Obj *list_ptr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(list_ptr);
    if (TCL_OK != ttrek_GetUseFlags(interp, spec_root, list_ptr)) {
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

    if (TCL_OK != ttrek_SetUseFlags(interp, spec_root, delObjc, delObjv)) {
        Tcl_DecrRefCount(list_ptr);
        Tcl_DecrRefCount(out_list_ptr);
        Tcl_DeleteHashTable(&ht);
        return TCL_ERROR;
    }

    Tcl_DeleteHashTable(&ht);
    Tcl_DecrRefCount(list_ptr);
    return TCL_OK;
}

int ttrek_HashTableContainsUseFlag(Tcl_Interp *interp, Tcl_HashTable *ht, const char *use_flag_str, int *contains_p) {

    UNUSED(interp);

    *contains_p = 0;

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
    if (entry) {
        int entry_polarity = PTR2INT(Tcl_GetHashValue(entry));
        if (entry_polarity == polarity) {
            *contains_p = 1;
            return TCL_OK;
        }
    }

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
        const char *use_flag_str = Tcl_GetString(elem);
        int contains = 0;
        if (TCL_OK != ttrek_HashTableContainsUseFlag(interp, ht, use_flag_str, &contains)) {
            return TCL_ERROR;
        }
        if (contains) {
            if (TCL_OK != Tcl_ListObjAppendElement(interp, result_list_ptr, elem)) {
                return TCL_ERROR;
            }
        }
    }

    return TCL_OK;
}