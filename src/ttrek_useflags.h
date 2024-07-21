/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_USEFLAGS_H
#define TTREK_USEFLAGS_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

int ttrek_GetUseFlags(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *list_ptr);
int ttrek_SetUseFlags(Tcl_Interp *interp, cJSON *spec_root, Tcl_Size objc, Tcl_Obj *const objv[]);
int ttrek_AddUseFlags(Tcl_Interp *interp, cJSON *spec_root, Tcl_Size objc, Tcl_Obj *const objv[]);
int ttrek_DelUseFlags(Tcl_Interp *interp, cJSON *spec_root, Tcl_Size objc, Tcl_Obj *const objv[]);
int ttrek_PopulateHashTableFromUseFlagsList(Tcl_Interp *interp, Tcl_Obj *list_ptr, Tcl_HashTable *ht);
int ttrek_PopulateUseFlagsListFromHashTable(Tcl_Interp *interp, Tcl_HashTable *ht, Tcl_Obj *list_ptr);
int ttrek_PopulateIUseFlagsListFromNode(Tcl_Interp *interp, cJSON *use_node, Tcl_Obj *list_ptr);
int ttrek_HashTableContainsUseFlag(Tcl_Interp *interp, Tcl_HashTable *ht, const char *use_flag_str, int *contains_p);
int ttrek_HashTableIntersectionWithIUse(Tcl_Interp *interp, Tcl_HashTable *ht, Tcl_Obj *iuse_list_ptr, Tcl_Obj *result_list_ptr);

#ifdef __cplusplus
}
#endif

#endif //TTREK_USEFLAGS_H