/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_SCRIPTS_H
#define TTREK_SCRIPTS_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

int ttrek_ScriptsAdd(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *name_ptr, Tcl_Obj *body_ptr);
int ttrek_ScriptsList(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj **list_ptr_ptr);
int ttrek_ScriptsDel(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *name_ptr);
int ttrek_ScriptsDescribe(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *name_ptr, Tcl_Obj **body_ptr_ptr);
int ttrek_ScriptsMakeTempFile(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *name_str, Tcl_Obj **file_ptr_ptr);
int ttrek_ScriptsDefineRunArgs(Tcl_Interp *interp, Tcl_Obj *file_ptr, Tcl_Size *argc_ptr, const char ***argv_ptr);

#ifdef __cplusplus
}
#endif

#endif //TTREK_SCRIPTS_H