/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include "registry.h"
#include "ttrek_telemetry.h"

int ttrek_VersionSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    UNUSED(objv);
    if (objc != 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Usage: ttrek version", -1));
        return TCL_ERROR;
    }

    const char *telemetry = "off";
    if (ttrek_TelemetryEnabled()) {
        if (ttrek_RegistrySelectForRead(interp) == TCL_OK) {
            telemetry = ttrek_TelemetryEnabled() ? "on" : "off";
        } else {
            // Version information remains available even in a broken project.
            telemetry = "unknown (cannot read registry configuration)";
            fprintf(stderr, "warning: telemetry status unavailable: %s\n", Tcl_GetStringResult(interp));
            Tcl_ResetResult(interp);
        }
    }

    const char *target = ttrek_ReleaseTarget();
    printf("ttrek %s\n", XSTR(PROJECT_VERSION));
    printf("Platform: %s\n", target ? target : "unknown");
    printf("Telemetry: %s\n", telemetry);
    printf("Debug: %s (TTREK_DEBUG)\n", ttrek_DebugEnabled() ? "on" : "off");
#ifdef DEBUG
    puts("Debug build: on");
#else
    puts("Debug build: off");
#endif
    return TCL_OK;
}
