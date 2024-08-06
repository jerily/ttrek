/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_GENINSTALL_H
#define TTREK_GENINSTALL_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

Tcl_Obj *ttrek_generateInstallScript(Tcl_Interp *interp, const char *package_name,
    const char *package_version, const char *project_build_dir,
    const char *project_install_dir, const char *source_dir,
    cJSON *spec, Tcl_HashTable *use_flags_ht_ptr, int is_local_build);

#ifdef __cplusplus
}
#endif

#endif //TTREK_GENINSTALL_H