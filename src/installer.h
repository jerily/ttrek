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

int ttrek_InstallPackage(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *package_name,
                         const char *package_version, const char *os, const char *arch,
                         const char *direct_version_requirement, int package_name_exists_in_lock_p,
                         int package_num_current, int package_num_total);

int ttrek_UninstallPackage(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *package_name);
int ttrek_DeleteTempFiles(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *package_name);
int ttrek_RestoreTempFiles(Tcl_Interp *interp, ttrek_state_t *state_ptr, const char *package_name);

#ifdef __cplusplus
}
#endif

#endif //TTREK_INSTALLER_H
