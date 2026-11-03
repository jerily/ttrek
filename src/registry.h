/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_REGISTRY_H
#define TTREK_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

int ttrek_RegistryGet(const char *url, Tcl_DString *dsPtr, cJSON *postData);
int ttrek_RegistrySetDirectory(Tcl_Interp *interp, const char *directory);
int ttrek_RegistryIsLocal(void);
const char *ttrek_RegistryGetDirectory(void);
int ttrek_RegistryLoadFromLock(Tcl_Interp *interp, Tcl_Obj *project, const cJSON *lock);
const char *ttrek_RegistryDefaultDirectory(void);
int ttrek_RegistryUseDefault(Tcl_Interp *interp);
int ttrek_RegistryGetLocal(const char *url, Tcl_DString *result);

#ifdef __cplusplus
}
#endif

#endif //TTREK_REGISTRY_H
