/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_FSMONITOR_H
#define TTREK_FSMONITOR_H

#include "../common.h"

typedef struct {
    Tcl_Obj *files_before;
    Tcl_Obj *files_diff;
} ttrek_fsmonitor_state_t;

int ttrek_FSMonitor_AddWatch(Tcl_Interp *interp, Tcl_Obj *project_install_dir_ptr, ttrek_fsmonitor_state_t *state_ptr);
int ttrek_FSMonitor_ReadChanges(Tcl_Interp *interp, Tcl_Obj *project_install_dir_ptr, ttrek_fsmonitor_state_t *state_ptr);
int ttrek_FSMonitor_RemoveWatch(Tcl_Interp *interp, ttrek_fsmonitor_state_t *state_ptr);

#endif //TTREK_FSMONITOR_H
