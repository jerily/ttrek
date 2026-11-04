#include "subCmdDecls.h"
#include "registry.h"

int ttrek_SearchSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    const char *registry = NULL;
    Tcl_ArgvInfo options[] = {
        {TCL_ARGV_STRING, "-registry", NULL, &registry, "local registry directory", NULL},
        {TCL_ARGV_END, NULL, NULL, NULL, NULL, NULL}
    };
    Tcl_Obj **remaining = NULL;
    if (Tcl_ParseArgsObjv(interp, options, &objc, objv, &remaining) != TCL_OK) { return TCL_ERROR; }
    int status = TCL_ERROR;
    Tcl_Obj *matches = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(matches);
    if (objc != 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Usage: ttrek search [-registry DIRECTORY] QUERY", -1));
        goto done;
    }
    if (ttrek_RegistrySetDirectory(interp, registry) != TCL_OK ||
        ttrek_RegistrySearchLocal(interp, Tcl_GetString(remaining[1]), matches) != TCL_OK) { goto done; }
    Tcl_Size count;
    Tcl_Obj **rows;
    if (Tcl_ListObjGetElements(interp, matches, &count, &rows) != TCL_OK) { goto done; }
    for (Tcl_Size i = 0; i < count; ++i) {
        Tcl_Obj *name, *description;
        if (Tcl_ListObjIndex(interp, rows[i], 0, &name) != TCL_OK ||
            Tcl_ListObjIndex(interp, rows[i], 1, &description) != TCL_OK) { goto done; }
        fprintf(stdout, "%s%s%s\n", Tcl_GetString(name), Tcl_GetCharLength(description) ? "\t" : "", Tcl_GetString(description));
    }
    status = TCL_OK;
done:
    Tcl_DecrRefCount(matches);
    ckfree(remaining);
    return status;
}
