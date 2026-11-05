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

// On failure, dsPtr (if supplied) contains a diagnostic for the caller to report.
int ttrek_RegistryGet(const char *url, Tcl_DString *dsPtr, cJSON *postData);
// Repeated options append sources in lookup order.
int ttrek_RegistryAddSource(Tcl_Interp *interp, const char *source);
Tcl_Size ttrek_RegistryOption(void *, Tcl_Interp *, Tcl_Size, Tcl_Obj *const *, void *);
void *ttrek_RegistryOptionHandler(void);
#define TTREK_REGISTRY_OPTIONS \
    {TCL_ARGV_GENFUNC, "-registry", ttrek_RegistryOptionHandler(), NULL, "append a registry directory or API URL", NULL}
int ttrek_RegistryHasSources(void);
int ttrek_RegistryIsLocal(void);
int ttrek_RegistrySourceCount(void);
const char *ttrek_RegistryLocalDirectory(int index);
void ttrek_RegistrySaveConfiguration(cJSON *lock);
int ttrek_RegistryLoadFromLock(Tcl_Interp *interp, Tcl_Obj *project, const cJSON *lock);
const char *ttrek_RegistryDefaultDirectory(void);
int ttrek_RegistryUseDefault(Tcl_Interp *interp);
// Internal transport result: only an absent package allows trying another source.
#define TTREK_REGISTRY_NOT_FOUND 404
int ttrek_RegistryGetConfigured(const char *url, Tcl_DString *result);
int ttrek_RegistryReadLocal(const char *directory, const char *url, Tcl_DString *result);
int ttrek_RegistryReadHTTP(const char *url, Tcl_DString *result);
int ttrek_RegistrySearchLocal(Tcl_Interp *interp, const char *query, Tcl_Obj *matches);

#ifdef __cplusplus
}
#endif

#endif //TTREK_REGISTRY_H
