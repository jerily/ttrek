/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include "ttrek_git.h"
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

static const char activation_script[] = {
#include "activate.sh.h"
    0
};

/* Environment values are data, including quotes, newlines and shell syntax. */
static void AppendShellArgument(Tcl_Obj *script, const char *value) {
    Tcl_AppendToObj(script, " '", -1);
    const char *start = value;
    for (const char *p = value; *p; ++p) {
        if (*p == '\'') {
            Tcl_AppendToObj(script, start, p - start);
            Tcl_AppendToObj(script, "'\\''", -1);
            start = p + 1;
        }
    }
    Tcl_AppendToObj(script, start, -1);
    Tcl_AppendToObj(script, "'", -1);
}

int ttrek_ActivateSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int option_user = 0, option_global = 0;
    Tcl_Obj **remaining = NULL;
    Tcl_ArgvInfo options[] = {
        {TCL_ARGV_CONSTANT, "-u", INT2PTR(1), &option_user, "activate in user mode", NULL},
        {TCL_ARGV_CONSTANT, "-g", INT2PTR(1), &option_global, "activate in global mode", NULL},
        {TCL_ARGV_END, NULL, NULL, NULL, NULL, NULL}
    };
    int rc = Tcl_ParseArgsObjv(interp, options, &objc, objv, &remaining);
    ckfree(remaining);
    if (rc != TCL_OK) { return rc; }
    if (option_user && option_global) {
        SetResult("conflicting options -u and -g");
        return TCL_ERROR;
    }
    if (objc != 1) {
        SetResult("usage: ttrek activate [-u|-g]");
        return TCL_ERROR;
    }

    ttrek_mode_t mode = option_user ? MODE_USER : (option_global ? MODE_GLOBAL : MODE_LOCAL);
    ttrek_state_t *state = ttrek_CreateState(interp, 0, 0, 0, mode, STRATEGY_LATEST);
    if (!state) { return TCL_ERROR; }
    if (ttrek_EnsureGitReady(interp, state) != TCL_OK) {
        ttrek_DestroyState(state);
        return TCL_ERROR;
    }

    /* An unlinked file survives exec through its descriptor. No startup files
     * are left behind, even if Bash fails to start or is killed. */
    FILE *file = tmpfile();
    int fd = file ? fcntl(fileno(file), F_DUPFD, 10) : -1;
    if (fd < 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot create Bash startup file: %s", Tcl_PosixError(interp)));
        if (file) { fclose(file); }
        ttrek_DestroyState(state);
        return TCL_ERROR;
    }
    Tcl_Obj *script = Tcl_ObjPrintf("exec %d<&-\n%s\n_ttrek_activate", fd, activation_script);
    Tcl_IncrRefCount(script);

    /* Use the same environment as run, but retain the caller's directory.
     * Apply it after .bashrc so startup customizations cannot erase activation. */
    ttrek_EnvironmentStateSetVenv(state);
    const char *names[] = {"PATH", "LD_LIBRARY_PATH", "PROJECT_HOME", "PROJECT_INSTALL_DIR"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        AppendShellArgument(script, getenv(names[i]));
    }
    AppendShellArgument(script, Tcl_GetString(state->project_venv_dir_ptr));
    Tcl_AppendToObj(script, "\nunset -f _ttrek_activate\n", -1);
    ttrek_EnvironmentStateRestore();
    ttrek_DestroyState(state);

    Tcl_Size length;
    const char *contents = Tcl_GetStringFromObj(script, &length);
    int failed = fwrite(contents, 1, length, file) != (size_t)length || fflush(file) != 0;
    Tcl_DecrRefCount(script);
    if (fclose(file) != 0) { failed = 1; }
    if (failed || lseek(fd, 0, SEEK_SET) < 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot write Bash startup file: %s", Tcl_PosixError(interp)));
        close(fd);
        return TCL_ERROR;
    }

    char path[64];
    snprintf(path, sizeof(path), "/dev/fd/%d", fd);
    /* Replace ttrek so Bash owns the terminal, job control and exit status. */
    fflush(NULL);
    execlp("bash", "bash", "--noprofile", "--rcfile", path, "-i", (char *)NULL);
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot start bash: %s", Tcl_PosixError(interp)));
    close(fd);
    return TCL_ERROR;
}
