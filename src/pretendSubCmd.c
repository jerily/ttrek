/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include "ttrek_resolvo.h"

int ttrek_PretendSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    ttrek_state_t *state_ptr = ttrek_CreateState(interp, MODE_LOCAL);
    if (state_ptr == NULL) {
        return TCL_ERROR;
    }
    int result = ttrek_pretend(interp, objc, objv, state_ptr);
    ttrek_DestroyState(state_ptr);
    return result;
}

