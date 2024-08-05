/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_BUILDINSTRUCTIONS_H
#define TTREK_BUILDINSTRUCTIONS_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

int ttrek_RunBuildInstructions(Tcl_Interp *interp, ttrek_state_t *state_ptr);

#ifdef __cplusplus
}
#endif

#endif //TTREK_BUILDINSTRUCTIONS_H