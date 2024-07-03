/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_TELEMETRY_H
#define TTREK_TELEMETRY_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

void ttrek_TelemetryFree(void);

void ttrek_TelemetryRegisterEnvironment();

Tcl_Obj *ttrek_TelemetryGetMachineId();

void ttrek_TelemetryLoadMachineId(Tcl_Interp *interp);
void ttrek_TelemetrySaveMachineId(Tcl_Interp *interp);

#ifdef __cplusplus
}
#endif

#endif //TTREK_TELEMETRY_H