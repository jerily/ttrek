#ifndef TTREK_RECIPE_DEPENDENCIES_H
#define TTREK_RECIPE_DEPENDENCIES_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The caller owns the returned recipe. */
cJSON *ttrek_FetchRecipe(Tcl_Interp *interp, const char *name, const char *version,
                        const char *os, const char *arch);

/* Append checks to a script initialized with _ttrek_missing=0. Finish with
 * [ "$_ttrek_missing" = 0 ] to report all failures before stopping. */
int ttrek_AppendRecipeChecks(Tcl_Interp *interp, Tcl_Obj *script, const char *name,
    const char *version, const cJSON *external, const cJSON *steps, Tcl_HashTable *flags);
int ttrek_RunRecipeChecks(Tcl_Interp *interp, Tcl_Obj *script);
int ttrek_CheckProjectDependencies(Tcl_Interp *interp, ttrek_state_t *state);

#ifdef __cplusplus
}
#endif
#endif
