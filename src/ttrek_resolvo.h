/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_RESOLVO_H
#define TTREK_RESOLVO_H


#ifdef __cplusplus
extern "C" {
#endif

int ttrek_pretend(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], ttrek_state_t *state_ptr);
int ttrek_install(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], ttrek_state_t *state_ptr);

#ifdef __cplusplus
}
#endif

#endif //TTREK_RESOLVO_H