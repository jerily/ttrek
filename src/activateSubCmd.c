/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include "ttrek_git.h"
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

static const char activation_script[] = {
#include "activate.sh.h"
    0
};

static const char zsh_activation_script[] = {
#include "activate.zsh.h"
    0
};

static const char *SupportedShell(const char *path) {
    if (!path) { return NULL; }
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (*name == '-') { ++name; } /* Login-shell process names. */
    if (strcmp(name, "bash") == 0) { return "bash"; }
    if (strcmp(name, "zsh") == 0) { return "zsh"; }
    return NULL;
}

static const char *ActivationShell(void) {
    char name[256] = {0};
#ifdef __linux__
    char path[64];
    snprintf(path, sizeof(path), "/proc/%ld/comm", (long)getppid());
    FILE *process = fopen(path, "r");
    if (process) {
        if (fgets(name, sizeof(name), process)) { name[strcspn(name, "\n")] = '\0'; }
        fclose(process);
    }
#elif defined(__APPLE__)
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getppid()};
    struct kinfo_proc process;
    size_t length = sizeof(process);
    if (sysctl(mib, 4, &process, &length, NULL, 0) == 0 && length == sizeof(process)) {
        snprintf(name, sizeof(name), "%s", process.kp_proc.p_comm);
    }
#endif
    const char *shell = SupportedShell(name);
    if (shell) { return shell; }
    shell = getenv("SHELL");
    return SupportedShell(shell) ? shell : "bash";
}

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

static void RemoveZshStartup(Tcl_Obj *directory) {
    const char *files[] = {".zshenv", ".zshrc"};
    for (size_t i = 0; i < 2; ++i) {
        Tcl_Obj *path = Tcl_ObjPrintf("%s/%s", Tcl_GetString(directory), files[i]);
        unlink(Tcl_GetString(path));
        Tcl_BounceRefCount(path);
    }
    rmdir(Tcl_GetString(directory));
    Tcl_DecrRefCount(directory);
}

/* Zsh has no --rcfile. Relay its user startup files through a private ZDOTDIR,
 * then restore the user's ZDOTDIR before reading their configuration. */
static Tcl_Obj *CreateZshStartup(Tcl_Interp *interp, int fd) {
    const char *temporary = getenv("TMPDIR");
    Tcl_Obj *base = Tcl_NewStringObj(temporary && *temporary ? temporary : "/tmp", -1);
    Tcl_IncrRefCount(base);
    Tcl_Obj *normalized = Tcl_FSGetNormalizedPath(interp, base);
    if (!normalized) { Tcl_DecrRefCount(base); return NULL; }
    Tcl_DString path;
    Tcl_DStringInit(&path);
    Tcl_DStringAppend(&path, Tcl_GetString(normalized), -1);
    Tcl_DStringAppend(&path, "/ttrek-activate-XXXXXX", -1);
    Tcl_DecrRefCount(base);
    if (!mkdtemp(Tcl_DStringValue(&path))) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot create Zsh startup directory: %s", Tcl_PosixError(interp)));
        Tcl_DStringFree(&path);
        return NULL;
    }
    Tcl_Obj *directory = Tcl_NewStringObj(Tcl_DStringValue(&path), -1);
    Tcl_IncrRefCount(directory);
    Tcl_DStringFree(&path);

    Tcl_Obj *env = Tcl_NewStringObj("", 0);
    Tcl_IncrRefCount(env);
    const char *original = getenv("ZDOTDIR");
    if (original) {
        Tcl_AppendToObj(env, "export", -1);
        Tcl_Obj *assignment = Tcl_ObjPrintf("ZDOTDIR=%s", original);
        AppendShellArgument(env, Tcl_GetString(assignment));
        Tcl_BounceRefCount(assignment);
    } else {
        Tcl_AppendToObj(env, "unset ZDOTDIR", -1);
    }
    Tcl_AppendToObj(env,
        "\nif [[ -r ${ZDOTDIR-${HOME}}/.zshenv ]]; then\n"
        "    source \"${ZDOTDIR-${HOME}}/.zshenv\"\n"
        "fi\n"
        "_TTREK_ZDOTDIR_RESTORE=$(typeset -p ZDOTDIR 2>/dev/null) || "
        "_TTREK_ZDOTDIR_RESTORE='unset ZDOTDIR'\n"
        "export", -1);
    Tcl_Obj *assignment = Tcl_ObjPrintf("ZDOTDIR=%s", Tcl_GetString(directory));
    AppendShellArgument(env, Tcl_GetString(assignment));
    Tcl_BounceRefCount(assignment);
    /* Even a user .zshenv that disables rc files still gets activation. */
    Tcl_AppendToObj(env, "\nif [[ ! -o rcs ]]; then source \"$ZDOTDIR/.zshrc\"; fi\n", -1);

    Tcl_Obj *rc = Tcl_NewStringObj(
        "_TTREK_STARTUP_DIR=$ZDOTDIR\n"
        "unset ZDOTDIR\n"
        "eval \"$_TTREK_ZDOTDIR_RESTORE\"\n"
        "unset _TTREK_ZDOTDIR_RESTORE\n"
        "/bin/rm -f -- \"$_TTREK_STARTUP_DIR/.zshenv\" \"$_TTREK_STARTUP_DIR/.zshrc\"\n"
        "/bin/rmdir -- \"$_TTREK_STARTUP_DIR\"\n"
        "unset _TTREK_STARTUP_DIR\n"
        "if [[ -o rcs && -r ${ZDOTDIR-${HOME}}/.zshrc ]]; then\n"
        "    source \"${ZDOTDIR-${HOME}}/.zshrc\"\n"
        "fi\n", -1);
    Tcl_IncrRefCount(rc);
    Tcl_AppendPrintfToObj(rc, "source /dev/fd/%d\n", fd);
    Tcl_Obj *env_path = Tcl_ObjPrintf("%s/.zshenv", Tcl_GetString(directory));
    Tcl_Obj *rc_path = Tcl_ObjPrintf("%s/.zshrc", Tcl_GetString(directory));
    Tcl_IncrRefCount(env_path);
    Tcl_IncrRefCount(rc_path);
    int status = ttrek_WriteChars(interp, env_path, env, 0600);
    if (status == TCL_OK) { status = ttrek_WriteChars(interp, rc_path, rc, 0600); }
    Tcl_DecrRefCount(env_path);
    Tcl_DecrRefCount(rc_path);
    Tcl_DecrRefCount(env);
    Tcl_DecrRefCount(rc);
    if (status != TCL_OK) { RemoveZshStartup(directory); return NULL; }
    return directory;
}

int ttrek_ActivateSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    const char *shell = ActivationShell();
    int is_zsh = strcmp(SupportedShell(shell), "zsh") == 0;
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

    /* An unlinked file survives exec through its descriptor. */
    FILE *file = tmpfile();
    int fd = file ? fcntl(fileno(file), F_DUPFD, 10) : -1;
    if (fd < 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot create shell startup file: %s", Tcl_PosixError(interp)));
        if (file) { fclose(file); }
        ttrek_DestroyState(state);
        return TCL_ERROR;
    }
    Tcl_Obj *script = is_zsh
        ? Tcl_ObjPrintf("_TTREK_STARTUP_FD=%d\nexec {_TTREK_STARTUP_FD}<&-\n"
                        "unset _TTREK_STARTUP_FD\n%s\n_ttrek_activate", fd, zsh_activation_script)
        : Tcl_ObjPrintf("exec %d<&-\n%s\n_ttrek_activate", fd, activation_script);
    Tcl_IncrRefCount(script);

    /* Use the same environment as run, but retain the caller's directory.
     * Apply it after the user's rc file so startup cannot erase activation. */
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
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot write shell startup file: %s", Tcl_PosixError(interp)));
        close(fd);
        return TCL_ERROR;
    }

    char path[64];
    snprintf(path, sizeof(path), "/dev/fd/%d", fd);
    Tcl_Obj *zsh_directory = NULL;
    if (is_zsh) {
        zsh_directory = CreateZshStartup(interp, fd);
        if (!zsh_directory) { close(fd); return TCL_ERROR; }
        if (setenv("ZDOTDIR", Tcl_GetString(zsh_directory), 1) != 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot set ZDOTDIR: %s", Tcl_PosixError(interp)));
            RemoveZshStartup(zsh_directory);
            close(fd);
            return TCL_ERROR;
        }
    }
    /* Replace ttrek so the shell owns the terminal, job control and exit status. */
    fflush(NULL);
    if (is_zsh) {
        execlp(shell, "zsh", "-i", (char *)NULL);
    } else {
        execlp(shell, "bash", "--noprofile", "--rcfile", path, "-i", (char *)NULL);
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot start %s: %s", shell, Tcl_PosixError(interp)));
    if (zsh_directory) { RemoveZshStartup(zsh_directory); }
    close(fd);
    return TCL_ERROR;
}
