/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <string.h>
#include "subCmdDecls.h"
#include "ttrek_help.h"

int ttrek_HelpSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    UNUSED(interp);

    const char *helpTopic = NULL;

    if (objc > 1 && strcmp(Tcl_GetString(objv[1]), "help") != 0) {

        helpTopic = ttrek_HelpGetTopicMessageFromObj(objv[1]);

        if (helpTopic == NULL) {
            fprintf(stderr, "error: unknown subcommand \"%s\"\n\n",
                Tcl_GetString(objv[1]));
        }

    }

    if (helpTopic == NULL) {
        helpTopic = ttrek_HelpGetTopicMessage("general");
    }

    fprintf(stdout, "%s\n", helpTopic);

    return TCL_OK;
}
