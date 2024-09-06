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
                                     const char *package_version, const char *source_dir,
                                     cJSON *spec, Tcl_HashTable *global_use_flags_ht_ptr,
                                     ttrek_state_t *state_ptr);

Tcl_Obj *ttrek_generateBootstrapScript(Tcl_Interp *interp, ttrek_state_t *state_ptr);

Tcl_Obj *ttrek_generatePackageCounter(Tcl_Interp *interp, int package_num_current, int package_num_total);

#ifdef __cplusplus
}
#endif

#endif //TTREK_GENINSTALL_H