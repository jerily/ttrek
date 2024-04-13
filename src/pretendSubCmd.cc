/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "subCmdDecls.h"
#include "resolvo/tests/solver.h"
#include <signal.h>


int ttrek_PretendSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    BundleBoxProvider provider;
    provider.requirements({"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"});

    return TCL_OK;
}
