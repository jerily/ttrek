/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_INSTALLER_H
#define TTREK_INSTALLER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

int ttrek_InstallPackage(Tcl_Interp *interp, const char *package_name, const char *package_version);

#ifdef __cplusplus
}
#endif

#endif //TTREK_INSTALLER_H
