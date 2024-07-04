/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_GIT_H
#define TTREK_GIT_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

int ttrek_GitInit(ttrek_state_t *state_ptr);
int ttrek_GitResetHard(ttrek_state_t *state_ptr);
int ttrek_GitCommit(ttrek_state_t *state_ptr, const char *message);
int ttrek_GitAmend(ttrek_state_t *state_ptr);
int ttrek_EnsureGitReady(Tcl_Interp *interp, ttrek_state_t *state_ptr);

#ifdef __cplusplus
}
#endif

#endif //TTREK_GIT_H