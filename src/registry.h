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
// The public fallback is resolved after all explicit or saved sources are loaded.
int *ttrek_RegistryPublicOption(void);
#define TTREK_REGISTRY_OPTIONS_HELP(source_help) \
    {TCL_ARGV_GENFUNC, "-registry", ttrek_RegistryOptionHandler(), NULL, source_help, NULL}, \
    {TCL_ARGV_CONSTANT, "-add-pubreg", INT2PTR(1), ttrek_RegistryPublicOption(), "append the default public registry", NULL}
#define TTREK_REGISTRY_OPTIONS TTREK_REGISTRY_OPTIONS_HELP("append a registry directory or API URL")
int ttrek_RegistryHasSources(void);
int ttrek_RegistryIsLocal(void);
void ttrek_RegistryUseWebsitesForSearch(void);
// Select explicit, nearest-project, or default sources without creating state.
int ttrek_RegistrySelectForRead(Tcl_Interp *interp);
void ttrek_RegistrySaveConfiguration(cJSON *lock);
int ttrek_RegistryLoadFromLock(Tcl_Interp *interp, Tcl_Obj *project, const cJSON *lock);
const char *ttrek_RegistryDefaultDirectory(void);
int ttrek_RegistryUseDefault(Tcl_Interp *interp);
// Internal transport result: only an absent package allows trying another source.
#define TTREK_REGISTRY_NOT_FOUND 404
#define TTREK_REGISTRY_TOO_LARGE 413
int ttrek_RegistryGetConfigured(const char *url, Tcl_DString *result);
int ttrek_RegistryReadLocal(const char *directory, const char *url, Tcl_DString *result);
int ttrek_RegistryProbeHTTP(const char *url, Tcl_DString *result);
int ttrek_RegistryPostHTTP(const char *url, Tcl_DString *result, cJSON *body);
int ttrek_RegistryPublicBatchRequest(const char *url, Tcl_DString *result, cJSON *body);
int ttrek_RegistryPlanRead(const char *url, Tcl_DString *result);
int ttrek_RegistryUsesBatchTelemetry(void);
#define TTREK_REGISTRY_BATCH_LIMIT 64
int ttrek_RegistryReadHTTP(const char *url, Tcl_DString *result);
int ttrek_RegistrySearch(Tcl_Interp *interp, const char *query, Tcl_Obj *matches);
int ttrek_LanguagesValid(const cJSON *languages);
// A new Tcl string containing either " [C, TCL]" or an empty string.
Tcl_Obj *ttrek_LanguagesLabel(const cJSON *languages);
// Optional metadata: caller owns the array; NULL means unavailable/unsupported.
cJSON *ttrek_RegistryLanguages(const char *name, const char *version);

#ifdef __cplusplus
}
#endif

#endif //TTREK_REGISTRY_H
