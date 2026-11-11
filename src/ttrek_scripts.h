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
// Returns an owned body object, or NULL when the script is absent.
int ttrek_ScriptsDescribe(Tcl_Interp *interp, ttrek_state_t *state_ptr, cJSON *source,
                          const char *name, Tcl_Obj **body_ptr_ptr);
int ttrek_ScriptsValidate(Tcl_Interp *interp, const cJSON *scripts);
int ttrek_ScriptsPackageSource(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *package_name, cJSON **source_ptr);
typedef struct {
    Tcl_Obj *path;
    const cJSON *args;
    int temporary;
} ttrek_script_t;

int ttrek_ScriptsPrepareRun(Tcl_Interp *interp, ttrek_state_t *state_ptr, cJSON *source,
                           const char *name, ttrek_script_t *script);
void ttrek_ScriptsRelease(ttrek_script_t *script);
int ttrek_ScriptsDefineRunArgs(Tcl_Interp *interp, const ttrek_script_t *script,
                              Tcl_Size *argc_ptr, const char ***argv_ptr);

#ifdef __cplusplus
}
#endif

#endif //TTREK_SCRIPTS_H
