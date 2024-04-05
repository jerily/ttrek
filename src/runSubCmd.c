#include "subCmdDecls.h"

int ttrek_RunSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *project_homedir_ptr = ttrek_GetProjectHomeDir(interp);
    if (!project_homedir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return TCL_ERROR;
    }

    fprintf(stderr, "project_homedir: %s\n", Tcl_GetString(project_homedir_ptr));

    Tcl_Obj *filename_ptr = Tcl_NewStringObj("local/bin/", -1);
    Tcl_IncrRefCount(filename_ptr);
    Tcl_AppendObjToObj(filename_ptr, objv[0]);
    Tcl_Obj *path_to_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_homedir_ptr, filename_ptr, &path_to_file_ptr)) {
        Tcl_DecrRefCount(filename_ptr);
        Tcl_DecrRefCount(project_homedir_ptr);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(filename_ptr);
    Tcl_IncrRefCount(path_to_file_ptr);

    Tcl_Size argc = objc;
    const char *argv[objc];
    argv[0] = Tcl_GetString(path_to_file_ptr);
    fprintf(stderr, "path_to_file: %s\n", Tcl_GetString(path_to_file_ptr));
    for (int i = 1; i < objc; i++) {
        argv[i] = Tcl_GetString(objv[i]);
    }
    if (TCL_OK != ttrek_ExecuteCommand(interp, argc, argv)) {
        return TCL_ERROR;
    }
    fprintf(stderr, "interp result: %s\n", Tcl_GetString(Tcl_GetObjResult(interp)));

    Tcl_DecrRefCount(project_homedir_ptr);
    Tcl_DecrRefCount(path_to_file_ptr);

    return TCL_OK;
}
