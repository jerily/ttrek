/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "common.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>
#include <time.h>
#include "cjson/cJSON.h"
#include <openssl/sha.h>

static int tjson_TreeToJson(Tcl_Interp *interp, cJSON *item, int num_spaces, Tcl_DString *dsPtr);

static struct {
    Tcl_Obj *ld_library_path;
    Tcl_Obj *path;
    Tcl_DString cwd;
    int cwd_initialized;
    int initialized;
} env_state = {
    .initialized = 0
};

static void ttrek_EnvironmentStateInit(void) {
    if (!env_state.initialized) {

        const char *env_var;

        env_var = getenv("LD_LIBRARY_PATH");
        if (env_var == NULL) {
            env_state.ld_library_path = NULL;
        } else {
            env_state.ld_library_path = Tcl_NewStringObj(env_var, -1);
            Tcl_IncrRefCount(env_state.ld_library_path);
        }

        env_var = getenv("PATH");
        if (env_var == NULL) {
            env_state.path = NULL;
        } else {
            env_state.path = Tcl_NewStringObj(env_var, -1);
            Tcl_IncrRefCount(env_state.path);
        }

        env_state.cwd_initialized = (Tcl_GetCwd(NULL, &env_state.cwd) != NULL);

        env_state.initialized = 1;

    }
}

void ttrek_EnvironmentStateFree(void) {
    if (env_state.initialized) {
        if (env_state.ld_library_path != NULL) {
            Tcl_DecrRefCount(env_state.ld_library_path);
        }
        if (env_state.path != NULL) {
            Tcl_DecrRefCount(env_state.path);
        }
        if (env_state.cwd_initialized) {
            Tcl_DStringFree(&env_state.cwd);
        }
        env_state.initialized = 0;
    }
}

void ttrek_EnvironmentStateSetVenv(ttrek_state_t *state_ptr) {

    ttrek_EnvironmentStateInit();

    Tcl_Obj *env_var;
    env_var = Tcl_ObjPrintf("%s/lib", Tcl_GetString(state_ptr->project_install_dir_ptr));
    DBG2(printf("setenv: %s = [%s]", "LD_LIBRARY_PATH", Tcl_GetString(env_var)));
    setenv("LD_LIBRARY_PATH", Tcl_GetString(env_var), 1);
    Tcl_BounceRefCount(env_var);

    if (env_state.path == NULL) {
        env_var = Tcl_ObjPrintf("%s/bin", Tcl_GetString(state_ptr->project_install_dir_ptr));
    } else {
        env_var = Tcl_ObjPrintf("%s/bin:%s", Tcl_GetString(state_ptr->project_install_dir_ptr),
            Tcl_GetString(env_state.path));
    }
    DBG2(printf("setenv: %s = [%s]", "PATH", Tcl_GetString(env_var)));
    setenv("PATH", Tcl_GetString(env_var), 1);
    Tcl_BounceRefCount(env_var);

    DBG2(printf("cd: [%s]", Tcl_GetString(state_ptr->project_install_dir_ptr)));
    Tcl_Chdir(Tcl_GetString(state_ptr->project_install_dir_ptr));

}

void ttrek_EnvironmentStateRestore(void) {

    if (env_state.ld_library_path == NULL) {
        unsetenv("LD_LIBRARY_PATH");
        DBG2(printf("unsetenv: %s", "LD_LIBRARY_PATH"));
    } else {
        DBG2(printf("setenv: %s = [%s]", "LD_LIBRARY_PATH", Tcl_GetString(env_state.ld_library_path)));
        setenv("LD_LIBRARY_PATH", Tcl_GetString(env_state.ld_library_path), 1);
    }

    if (env_state.path == NULL) {
        unsetenv("PATH");
        DBG2(printf("unsetenv: %s", "PATH"));
    } else {
        DBG2(printf("setenv: %s = [%s]", "PATH", Tcl_GetString(env_state.path)));
        setenv("PATH", Tcl_GetString(env_state.path), 1);
    }

    if (env_state.cwd_initialized) {
        DBG2(printf("cd: [%s]", Tcl_DStringValue(&env_state.cwd)));
        Tcl_Chdir(Tcl_DStringValue(&env_state.cwd));
    } else {
        DBG2(printf("cd: <there is no known directory>"));
    }

}

Tcl_Obj *ttrek_GetHashSHA256(Tcl_Obj *data_ptr) {
    Tcl_Size size;
    unsigned char *str = Tcl_GetByteArrayFromObj(data_ptr, &size);
    DBG2(printf("get SHA256 hash from data size: %" TCL_SIZE_MODIFIER "d", size));

    unsigned char hash_bin[SHA256_DIGEST_LENGTH];
    SHA256(str, size, hash_bin);

    const char *hex = "0123456789abcdef";
    char hash_hex[SHA256_DIGEST_LENGTH * 2];
    for (int i = 0, j = 0; i < SHA256_DIGEST_LENGTH; i++) {
        hash_hex[j++] = hex[(hash_bin[i] >> 4) & 0xF];
        hash_hex[j++] = hex[hash_bin[i] & 0xF];
    }

    Tcl_Obj *rc = Tcl_NewStringObj(hash_hex, SHA256_DIGEST_LENGTH * 2);
    DBG2(printf("return: %s", Tcl_GetString(rc)));
    return rc;
}

Tcl_Obj *ttrek_GetHomeDirectory() {
    const char *homeDir = getenv("HOME");
    if (homeDir == NULL) {
        DBG2(printf("failed to get HOME env var"));
        return NULL;
    }
    Tcl_Obj *homeDirObj = Tcl_NewStringObj(homeDir, -1);
    Tcl_Obj *objv[1] = {
        Tcl_NewStringObj(".ttrek", -1)
    };
    homeDirObj = Tcl_FSJoinToPath(homeDirObj, 1, objv);
    DBG2(printf("return [%s]", Tcl_GetString(homeDirObj)));
    return homeDirObj;
}

int ttrek_ResolvePath(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj *filename_ptr, Tcl_Obj **output_path_ptr) {
    Tcl_Obj *objv[1] = {filename_ptr};
    *output_path_ptr = Tcl_FSJoinToPath(path_ptr, 1, objv);

    if (!*output_path_ptr) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not resolve path for %s",
            Tcl_GetString(filename_ptr)));
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(*output_path_ptr);
//    *path_ptr = Tcl_FSGetNormalizedPath(interp, *path_ptr);

    return TCL_OK;
}

int ttrek_CheckFileExists(Tcl_Obj *path_ptr) {
    if (Tcl_FSAccess(path_ptr, F_OK) != 0) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

int ttrek_FileExists(Tcl_Interp *interp, Tcl_Obj *path_ptr, int *exists) {
    UNUSED(interp);
    if (TCL_OK != ttrek_CheckFileExists(path_ptr)) {
        *exists = 0;
    } else {
        *exists = 1;
    }
    return TCL_OK;
}

int ttrek_DirectoryExists(Tcl_Interp *interp, Tcl_Obj *dir_path_ptr, int *result_exists) {
    int exists = 0;
    *result_exists = 0;
    if (TCL_OK != ttrek_FileExists(interp, dir_path_ptr, &exists)) {
        fprintf(stderr, "error: could not check if directory exists\n");
        return TCL_ERROR;
    }

    if (exists) {
        // Make sure the specified path is a directory.
        Tcl_StatBuf *sb = Tcl_AllocStatBuf();
        if (sb == NULL) {
            DBG2(printf("unable to alloc Tcl_StatBuf"));
            return TCL_ERROR;
        }
        if (Tcl_FSStat(dir_path_ptr, sb) != 0) {
            ckfree(sb);
            DBG2(printf("unable get stats for the path"));
            return TCL_ERROR;
        }
        if (!S_ISDIR(Tcl_GetModeFromStat(sb))) {
            ckfree(sb);
            DBG2(printf("ERROR: the path is not a directory"));
            return TCL_ERROR;
        }
        ckfree(sb);
        *result_exists = 1;
        return TCL_OK;
    }
    return TCL_OK;
}

int ttrek_EnsureDirectoryExists(Tcl_Interp *interp, Tcl_Obj *dir_path_ptr) {
    int exists = 0;
    if (TCL_OK != ttrek_DirectoryExists(interp, dir_path_ptr, &exists)) {
        fprintf(stderr, "error: could not check if directory exists\n");
        return TCL_ERROR;
    }

    if (exists) {
        return TCL_OK;
    }

    if (TCL_OK != Tcl_FSCreateDirectory(dir_path_ptr)) {
        fprintf(stderr, "error: could not create directory\n");
        return TCL_ERROR;
    }

    return TCL_OK;
}

int ttrek_EnsureSkeletonExists(Tcl_Interp *interp, ttrek_state_t *state_ptr) {
    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, state_ptr->project_home_dir_ptr)) {
        fprintf(stderr, "error: could not ensure project home directory exists\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, state_ptr->project_venv_dir_ptr)) {
        fprintf(stderr, "error: could not ensure project venv directory exists\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, state_ptr->project_build_dir_ptr)) {
        fprintf(stderr, "error: could not ensure project build directory exists\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, state_ptr->project_install_dir_ptr)) {
        fprintf(stderr, "error: could not ensure project install directory exists\n");
        return TCL_ERROR;
    }

    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, state_ptr->project_temp_dir_ptr)) {
        fprintf(stderr, "error: could not ensure project temp directory exists\n");
        return TCL_ERROR;
    }

    return TCL_OK;
}

int ttrek_WriteChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj *contents_ptr, int permissions) {
    Tcl_Channel chan = Tcl_OpenFileChannel(interp, Tcl_GetString(path_ptr), "w", permissions);
    Tcl_SetChannelOption(interp, chan, "-encoding", "utf-8");
    if (!chan) {
        fprintf(stderr, "error: could not open %s\n", Tcl_GetString(path_ptr));
        return TCL_ERROR;
    }
    Tcl_WriteObj(chan, contents_ptr);
    Tcl_Close(interp, chan);
    return TCL_OK;
}

int ttrek_WriteJsonFile(Tcl_Interp *interp, Tcl_Obj *path_ptr, cJSON *root) {
    Tcl_DString ds;
    Tcl_DStringInit(&ds);
    if (TCL_OK != tjson_TreeToJson(interp, root, 2, &ds)) {
        Tcl_DStringFree(&ds);
        return TCL_ERROR;
    }
    Tcl_Obj *contents_ptr = Tcl_DStringToObj(&ds);
    Tcl_IncrRefCount(contents_ptr);
    Tcl_DStringFree(&ds);
    int result = ttrek_WriteChars(interp, path_ptr, contents_ptr, 0666);
    Tcl_DecrRefCount(contents_ptr);
    return result;
}

int ttrek_ReadChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj **contents_ptr) {
    Tcl_Channel chan = Tcl_OpenFileChannel(interp, Tcl_GetString(path_ptr), "r", 0666);
    Tcl_SetChannelOption(interp, chan, "-encoding", "utf-8");
    if (!chan) {
        fprintf(stderr, "error: could not open %s\n", Tcl_GetString(path_ptr));
        return TCL_ERROR;
    }
    Tcl_ReadChars(chan, *contents_ptr, -1, 0);
    Tcl_Close(interp, chan);
    return TCL_OK;
}

int ttrek_FileToJson(Tcl_Interp *interp, Tcl_Obj *path_ptr, cJSON **root) {
    Tcl_Obj *contents_ptr = Tcl_NewStringObj("", -1);
    Tcl_IncrRefCount(contents_ptr);
    if (TCL_OK != ttrek_ReadChars(interp, path_ptr, &contents_ptr)) {
        fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_ptr));
        Tcl_DecrRefCount(contents_ptr);
        return TCL_ERROR;
    }
    *root = cJSON_Parse(Tcl_GetString(contents_ptr));
    Tcl_DecrRefCount(contents_ptr);
    return TCL_OK;
}

int ttrek_ExecuteCommand(Tcl_Interp *interp, Tcl_Size argc, const char *argv[], Tcl_Obj *resultObj) {
    void *handle;
    int rc;
    Tcl_ResetResult(interp);
    Tcl_Channel chan = Tcl_OpenCommandChannel(interp, argc, argv, TCL_STDOUT);
    if (!chan) {
        SetResult("could not open command channel");
        return TCL_ERROR;
    }
    if (Tcl_GetChannelHandle(chan, TCL_READABLE, &handle) != TCL_OK) {
        SetResult("could not get channel handle");
        return TCL_ERROR;
    }

    // sleep for 100ms
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 100000000;

    if (resultObj != NULL) {
        while (!Tcl_Eof(chan)) {
            if (Tcl_ReadChars(chan, resultObj, -1, 0) < 0) {
                goto read_error;
            }
            nanosleep(&ts, NULL);
        }
        goto wait;
    }

    // make "chan" non-blocking
    Tcl_SetChannelOption(interp, chan, "-blocking", "0");
    Tcl_SetChannelOption(interp, chan, "-buffering", "none");
    Tcl_SetChannelOption(interp, chan, "-translation", "binary");

    resultObj = Tcl_NewObj();
    Tcl_IncrRefCount(resultObj);
    while (!Tcl_Eof(chan)) {
        if (Tcl_ReadChars(chan, resultObj, -1, 0) < 0) {
            Tcl_DecrRefCount(resultObj);
            goto read_error;
        }
        if (Tcl_GetCharLength(resultObj) > 0) {
            fprintf(stdout, "%s", Tcl_GetString(resultObj));
            fflush(stdout);
        }
        nanosleep(&ts, NULL);
    }
    Tcl_DecrRefCount(resultObj);
    resultObj = NULL;

    // make "chan" blocking to properly detect a possible runtime error
    // during Tcl_Close();
    Tcl_SetChannelOption(interp, chan, "-blocking", "1");

wait:
    rc = Tcl_Close(interp, chan);

    // If we were called in a mode where we show the output of a command,
    // try to show the exact reason why the command failed.
    if (rc != TCL_OK && resultObj == NULL) {
        fprintf(stderr, "Interp result: %s\n", Tcl_GetString(Tcl_GetObjResult(interp)));
        Tcl_Obj *options = Tcl_GetReturnOptions(interp, rc);
        Tcl_Obj *key = Tcl_NewStringObj("-errorcode", -1);
        Tcl_IncrRefCount(key);
        Tcl_Obj *errorCode;
        Tcl_DictObjGet(NULL, options, key, &errorCode);
        Tcl_DecrRefCount(key);
        fprintf(stderr, "Exit status: %s\n", Tcl_GetString(errorCode));
        Tcl_DecrRefCount(options);
    }

    if (resultObj != NULL) {
        Tcl_Size len;
        char *str;
        // If the process produced anything on stderr, it will have been
        // returned in the interpreter result.  It needs to be appended to
        // the result string.
        str = Tcl_GetStringFromObj(Tcl_GetObjResult(interp), &len);
        Tcl_AppendToObj(resultObj, str, len);
        // If the last character of the result is a newline, then remove
        // the newline character.
        str = Tcl_GetStringFromObj(resultObj, &len);
        if (len > 0 && str[len - 1] == '\n') {
            Tcl_SetObjLength(resultObj, len - 1);
        }
    }

    return rc;

read_error:
    Tcl_ResetResult(interp);
    Tcl_AppendResult(interp, "error reading output from command: ",
        Tcl_PosixError(interp), (char *) NULL);
    return TCL_ERROR;
}

Tcl_Obj *ttrek_GetProjectDirForLocalMode(Tcl_Interp *interp) {
    Tcl_Obj *project_homedir_ptr = Tcl_FSGetCwd(interp);
    if (!project_homedir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return NULL;
    }
    Tcl_IncrRefCount(project_homedir_ptr);
    Tcl_Obj *project_filename_ptr = Tcl_NewStringObj(SPEC_JSON_FILE, -1);
    Tcl_IncrRefCount(project_filename_ptr);
    Tcl_Obj *path_project_file_ptr;
    while (Tcl_GetCharLength(project_homedir_ptr) > 0) {

        if (TCL_OK != ttrek_ResolvePath(interp, project_homedir_ptr, project_filename_ptr, &path_project_file_ptr)) {
            fprintf(stderr, "here0\n");
            Tcl_DecrRefCount(project_filename_ptr);
            Tcl_DecrRefCount(project_homedir_ptr);
            return NULL;
        }

        DBG(fprintf(stderr, "(candidate) path_project_file: %s\n", Tcl_GetString(path_project_file_ptr)));

        if (!path_project_file_ptr) {
            fprintf(stderr, "here1\n");
            Tcl_DecrRefCount(project_filename_ptr);
            Tcl_DecrRefCount(project_homedir_ptr);
            return NULL;
        }
        Tcl_IncrRefCount(path_project_file_ptr);

        if (ttrek_CheckFileExists(path_project_file_ptr) == TCL_OK) {
            Tcl_DecrRefCount(project_filename_ptr);
            Tcl_DecrRefCount(path_project_file_ptr);
            Tcl_IncrRefCount(project_homedir_ptr);
            return project_homedir_ptr;
        }
        Tcl_DecrRefCount(path_project_file_ptr);

        Tcl_Size list_length;
        Tcl_Obj *list_ptr = Tcl_FSSplitPath(project_homedir_ptr, &list_length);
        if (!list_ptr || !list_length) {
            return NULL;
        }
        DBG(fprintf(stderr, "GetProjectHomeDir: list_length: %zd list: %s\n", list_length, Tcl_GetString(list_ptr)));
        Tcl_DecrRefCount(project_homedir_ptr);
        Tcl_IncrRefCount(list_ptr);
        project_homedir_ptr = Tcl_FSJoinPath(list_ptr, list_length - 1);
        Tcl_IncrRefCount(project_homedir_ptr);
        Tcl_DecrRefCount(list_ptr);
    }
    Tcl_DecrRefCount(project_filename_ptr);
    Tcl_DecrRefCount(project_homedir_ptr);
    fprintf(stderr, "error: %s does not exist, run 'ttrek init' first\n", SPEC_JSON_FILE);
    return NULL;
}

Tcl_Obj *ttrek_GetProjectDirForUserMode(Tcl_Interp *interp) {
    Tcl_Obj *project_homedir_ptr = ttrek_GetHomeDirectory();
    if (!project_homedir_ptr) {
        fprintf(stderr, "error: getting home directory failed\n");
        return NULL;
    }
    Tcl_IncrRefCount(project_homedir_ptr);

    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, project_homedir_ptr)) {
        fprintf(stderr, "error: could not ensure project %s/.ttrek directory exists\n", Tcl_GetString(project_homedir_ptr));
        return NULL;
    }

    return project_homedir_ptr;
}

Tcl_Obj *ttrek_GetProjectDirForGlobalMode(Tcl_Interp *interp) {
    Tcl_Obj *project_homedir_ptr = Tcl_NewStringObj("/usr/local/share/ttrek", -1);
    if (!project_homedir_ptr) {
        fprintf(stderr, "error: getting home directory failed\n");
        return NULL;
    }
    Tcl_IncrRefCount(project_homedir_ptr);

    if (TCL_OK != ttrek_EnsureDirectoryExists(interp, project_homedir_ptr)) {
        fprintf(stderr, "error: could not ensure project %s/.ttrek directory exists\n", Tcl_GetString(project_homedir_ptr));
        return NULL;
    }

    return project_homedir_ptr;
}

Tcl_Obj *ttrek_GetProjectHomeDir(Tcl_Interp *interp, ttrek_mode_t mode) {
    if (mode == MODE_LOCAL) {
        return ttrek_GetProjectDirForLocalMode(interp);
    } else if (mode == MODE_USER) {
        return ttrek_GetProjectDirForUserMode(interp);
    } else if (mode == MODE_GLOBAL) {
        return ttrek_GetProjectDirForGlobalMode(interp);
    }
    return NULL;
}

Tcl_Obj *ttrek_GetProjectVenvDir(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr) {
    Tcl_Obj *venv_dir_ptr = Tcl_NewStringObj(VENV_DIR, -1);
    Tcl_IncrRefCount(venv_dir_ptr);
    Tcl_Obj *path_to_venv_dir_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_home_dir_ptr, venv_dir_ptr, &path_to_venv_dir_ptr)) {
        Tcl_DecrRefCount(venv_dir_ptr);
        return NULL;
    }
    Tcl_DecrRefCount(venv_dir_ptr);
    Tcl_IncrRefCount(path_to_venv_dir_ptr);
    return path_to_venv_dir_ptr;
}

Tcl_Obj *ttrek_GetVenvSubDir(Tcl_Interp *interp, Tcl_Obj *project_venv_dir_ptr, const char *subdir) {
    Tcl_Obj *install_dir_ptr = Tcl_NewStringObj(subdir, -1);
    Tcl_IncrRefCount(install_dir_ptr);
    Tcl_Obj *path_to_install_dir_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_venv_dir_ptr, install_dir_ptr, &path_to_install_dir_ptr)) {
        Tcl_DecrRefCount(install_dir_ptr);
        return NULL;
    }
    Tcl_DecrRefCount(install_dir_ptr);
    Tcl_IncrRefCount(path_to_install_dir_ptr);
    return path_to_install_dir_ptr;
}

Tcl_Obj *ttrek_GetFilePath(Tcl_Interp *interp, Tcl_Obj *project_venv_dir_ptr, const char *filename) {
    Tcl_Obj *filename_ptr = Tcl_NewStringObj(filename, -1);
    Tcl_IncrRefCount(filename_ptr);
    Tcl_Obj *path_to_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, project_venv_dir_ptr, filename_ptr, &path_to_file_ptr)) {
        Tcl_DecrRefCount(filename_ptr);
        return NULL;
    }
    Tcl_DecrRefCount(filename_ptr);
    Tcl_IncrRefCount(path_to_file_ptr);
    return path_to_file_ptr;
}

cJSON *ttrek_GetSpecRoot(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr) {
    Tcl_Obj *path_to_spec_file_ptr = ttrek_GetFilePath(interp, project_home_dir_ptr, SPEC_JSON_FILE);
    cJSON *spec_root = NULL;
    if (TCL_OK != ttrek_FileToJson(interp, path_to_spec_file_ptr, &spec_root)) {
        fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_to_spec_file_ptr));
        Tcl_DecrRefCount(path_to_spec_file_ptr);
        return NULL;
    }
    Tcl_DecrRefCount(path_to_spec_file_ptr);
    return spec_root;
}

cJSON *ttrek_GetLockRoot(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr) {

    Tcl_Obj *path_to_lock_file_ptr = ttrek_GetFilePath(interp, project_home_dir_ptr, LOCK_JSON_FILE);

    cJSON *lock_root = NULL;
    if (TCL_OK == ttrek_CheckFileExists(path_to_lock_file_ptr)) {
        if (TCL_OK != ttrek_FileToJson(interp, path_to_lock_file_ptr, &lock_root)) {
            fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_to_lock_file_ptr));
            Tcl_DecrRefCount(path_to_lock_file_ptr);
            return NULL;
        }
    } else {
        lock_root = cJSON_CreateObject();
    }

    Tcl_DecrRefCount(path_to_lock_file_ptr);

    return lock_root;
}

cJSON *ttrek_GetManifestRoot(Tcl_Interp *interp, Tcl_Obj *project_venv_dir_ptr) {

    Tcl_Obj *path_to_manifest_file_ptr = ttrek_GetFilePath(interp, project_venv_dir_ptr, MANIFEST_JSON_FILE);

    cJSON *manifest_root = NULL;
    if (TCL_OK == ttrek_CheckFileExists(path_to_manifest_file_ptr)) {
        if (TCL_OK != ttrek_FileToJson(interp, path_to_manifest_file_ptr, &manifest_root)) {
            fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_to_manifest_file_ptr));
            Tcl_DecrRefCount(path_to_manifest_file_ptr);
            return NULL;
        }
    } else {
        manifest_root = cJSON_CreateObject();
    }

    Tcl_DecrRefCount(path_to_manifest_file_ptr);

    return manifest_root;
}

static int ttrek_LockFile(ttrek_state_t *state_ptr) {
    int fd = open(Tcl_GetString(state_ptr->locking_file_path_ptr), O_WRONLY | O_CREAT, 0666);
    if (fd == -1) {
        fprintf(stderr, "error: creating lock file failed\n");
        return TCL_ERROR;
    }

    // attempt to acquire an exclusive lock
    if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
        fprintf(stderr, "error: acquiring lock failed - some other process is running\n");
        close(fd);
        return TCL_ERROR;
    }
    state_ptr->lock_fd = fd;
    return TCL_OK;
}

static int ttrek_UnlockFile(ttrek_state_t *state_ptr) {
    // attempt to acquire an exclusive lock
    if (flock(state_ptr->lock_fd, LOCK_UN) == -1) {
        fprintf(stderr, "error: acquiring lock failed - some other process is running\n");
        close(state_ptr->lock_fd);
        return TCL_ERROR;
    }
    return TCL_OK;
}

ttrek_state_t *ttrek_CreateState(Tcl_Interp *interp, int option_yes, int option_force, int with_locking, ttrek_mode_t mode, ttrek_strategy_t strategy) {
    ttrek_state_t *state_ptr = (ttrek_state_t *) Tcl_Alloc(sizeof(ttrek_state_t));
    if (!state_ptr) {
        return NULL;
    }
    DBG2(printf("allocated state: %p", (void *)state_ptr));

    state_ptr->interp = interp;

    Tcl_Obj *project_home_dir_ptr = ttrek_GetProjectHomeDir(interp, mode);
    if (!project_home_dir_ptr) {
        fprintf(stderr, "error: getting project home directory failed\n");
        return NULL;
    }

    fprintf(stderr, "project_home_dir: %s\n", Tcl_GetString(project_home_dir_ptr));

    Tcl_Obj *path_to_spec_file_ptr = ttrek_GetFilePath(interp, project_home_dir_ptr, SPEC_JSON_FILE);

    if (TCL_OK != ttrek_CheckFileExists(path_to_spec_file_ptr)) {
        if (mode == MODE_LOCAL) {
            fprintf(stderr, "%s does not exist, run 'ttrek init' first\n", SPEC_JSON_FILE);
            Tcl_DecrRefCount(project_home_dir_ptr);
            Tcl_DecrRefCount(path_to_spec_file_ptr);
            return NULL;
        } else {
            ttrek_InitSpecFile(interp, path_to_spec_file_ptr, "system-created-project", "1.0.0");
        }
    }

    Tcl_Obj *path_lock_file_ptr = ttrek_GetFilePath(interp, project_home_dir_ptr, LOCK_JSON_FILE);

    if (TCL_OK != ttrek_CheckFileExists(path_lock_file_ptr)) {
        if (mode != MODE_LOCAL) {
            ttrek_InitLockFile(interp, path_lock_file_ptr);
        }
    }

    Tcl_Obj *project_venv_dir_ptr = ttrek_GetProjectVenvDir(interp, project_home_dir_ptr);

    state_ptr->with_locking = with_locking;
    state_ptr->option_yes = option_yes;
    state_ptr->option_force = option_force;
    state_ptr->mode = mode;
    state_ptr->strategy = strategy;
    state_ptr->project_home_dir_ptr = project_home_dir_ptr;
    state_ptr->project_venv_dir_ptr = project_venv_dir_ptr;
    state_ptr->project_install_dir_ptr = ttrek_GetVenvSubDir(interp, project_venv_dir_ptr, INSTALL_DIR);
    state_ptr->project_build_dir_ptr = ttrek_GetVenvSubDir(interp, project_venv_dir_ptr, BUILD_DIR);
    state_ptr->project_temp_dir_ptr = ttrek_GetVenvSubDir(interp, project_venv_dir_ptr, TEMP_DIR);
    state_ptr->dirty_file_path_ptr = ttrek_GetFilePath(interp, project_venv_dir_ptr, DIRTY_FILE);
    state_ptr->locking_file_path_ptr = ttrek_GetFilePath(interp, project_venv_dir_ptr, LOCKING_FILE);
    // we do not check if the manifest file exists here
    // because it is not required to exist
    state_ptr->manifest_json_path_ptr = ttrek_GetFilePath(interp, project_venv_dir_ptr, MANIFEST_JSON_FILE);
    state_ptr->spec_json_path_ptr = path_to_spec_file_ptr;
    state_ptr->lock_json_path_ptr = path_lock_file_ptr;
    state_ptr->spec_root = ttrek_GetSpecRoot(interp, project_home_dir_ptr);
    state_ptr->lock_root = ttrek_GetLockRoot(interp, project_home_dir_ptr);
    state_ptr->manifest_root = ttrek_GetManifestRoot(interp, project_venv_dir_ptr);

    // print all refCount for all dir_ptr in state_ptr
    DBG(fprintf(stderr, "project_home_dir_ptr refCount: %" TCL_SIZE_MODIFIER "d\n",
        state_ptr->project_home_dir_ptr->refCount));
    DBG(fprintf(stderr, "project_venv_dir_ptr refCount: %" TCL_SIZE_MODIFIER "d\n",
        state_ptr->project_venv_dir_ptr->refCount));
    DBG(fprintf(stderr, "project_install_dir_ptr refCount: %" TCL_SIZE_MODIFIER "d\n",
        state_ptr->project_install_dir_ptr->refCount));
    DBG(fprintf(stderr, "project_build_dir_ptr refCount: %" TCL_SIZE_MODIFIER "d\n",
        state_ptr->project_build_dir_ptr->refCount));
    DBG(fprintf(stderr, "project_temp_dir_ptr refCount: %" TCL_SIZE_MODIFIER "d\n",
        state_ptr->project_temp_dir_ptr->refCount));
    DBG(fprintf(stderr, "dirty_file_path_ptr refCount: %" TCL_SIZE_MODIFIER "d\n",
        state_ptr->dirty_file_path_ptr->refCount));
    DBG(fprintf(stderr, "spec_json_path_ptr refCount: %" TCL_SIZE_MODIFIER "d\n",
        state_ptr->spec_json_path_ptr->refCount));
    DBG(fprintf(stderr, "lock_json_path_ptr refCount: %" TCL_SIZE_MODIFIER "d\n",
        state_ptr->lock_json_path_ptr->refCount));
    DBG(fprintf(stderr, "manifest_json_path_ptr refCount: %" TCL_SIZE_MODIFIER "d\n",
        state_ptr->manifest_json_path_ptr->refCount));

    if (TCL_OK != ttrek_EnsureSkeletonExists(interp, state_ptr)) {
        fprintf(stderr, "error: could not ensure skeleton exists\n");
        return NULL;
    }

    if (state_ptr->with_locking) {
        if (TCL_OK != ttrek_LockFile(state_ptr)) {
            fprintf(stderr, "error: could not lock file\n");
            return NULL;
        }
    }

    return state_ptr;
}

void ttrek_DestroyState(ttrek_state_t *state_ptr) {
    DBG2(printf("enter"));
    if (state_ptr->with_locking) {
        // DBG2(printf("unlock the state"));
        ttrek_UnlockFile(state_ptr);
    } else {
        // DBG2(printf("the state is not locked"));
    }
    // DBG2(printf("release Tcl objects"));
    Tcl_DecrRefCount(state_ptr->project_home_dir_ptr);
    Tcl_DecrRefCount(state_ptr->project_venv_dir_ptr);
    Tcl_DecrRefCount(state_ptr->project_install_dir_ptr);
    Tcl_DecrRefCount(state_ptr->project_build_dir_ptr);
    Tcl_DecrRefCount(state_ptr->project_temp_dir_ptr);
    Tcl_DecrRefCount(state_ptr->spec_json_path_ptr);
    Tcl_DecrRefCount(state_ptr->lock_json_path_ptr);
    Tcl_DecrRefCount(state_ptr->manifest_json_path_ptr);
    // DBG2(printf("release spec_root: %p", (void *)state_ptr->spec_root));
    cJSON_Delete(state_ptr->spec_root);
    // DBG2(printf("release lock_root: %p", (void *)state_ptr->lock_root));
    cJSON_Delete(state_ptr->lock_root);
    // DBG2(printf("release manifest_root: %p", (void *)state_ptr->manifest_root));
    cJSON_Delete(state_ptr->manifest_root);
    DBG2(printf("release state: %p", (void *)state_ptr));
    Tcl_Free((char *) state_ptr);
    DBG2(printf("return: ok"));
}

#define MAX_STRATEGY_LEN 7
ttrek_strategy_t ttrek_StrategyFromString(const char *strategy_str, ttrek_strategy_t default_strategy) {
    if (strategy_str == NULL) {
        return default_strategy;
    }

    if (strncmp(strategy_str, "latest", MAX_STRATEGY_LEN) == 0) {
        return STRATEGY_LATEST;
    } else if (strncmp(strategy_str, "favored", MAX_STRATEGY_LEN) == 0) {
        return STRATEGY_FAVORED;
    } else if (strncmp(strategy_str, "locked", MAX_STRATEGY_LEN) == 0) {
        return STRATEGY_LOCKED;
    }
    return STRATEGY_LATEST;
}


int ttrek_GetDirectDependencies(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *list_ptr) {
    cJSON *deps = cJSON_GetObjectItem(spec_root, "dependencies");
    if (!deps) {
        return TCL_ERROR;
    }

    cJSON *dep = NULL;
    for (int i = 0; i < cJSON_GetArraySize(deps); i++) {
        dep = cJSON_GetArrayItem(deps, i);
        char spec[256];
        snprintf(spec, 256, "%s@%s", dep->string, dep->valuestring);
        DBG(fprintf(stderr, "spec: %s\n", spec));
        if (TCL_OK != Tcl_ListObjAppendElement(interp, list_ptr, Tcl_NewStringObj(dep->string, -1))) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}


#define SP " "
#define NL "\n"
#define LBRACKET "["
#define RBRACKET "]"
#define LBRACE "{"
#define RBRACE "}"
#define COMMA ","

static int tjson_EscapeJsonString(Tcl_Obj *objPtr, Tcl_DString *dsPtr) {
    Tcl_Size length;
    const char *str = Tcl_GetStringFromObj(objPtr, &length);
    // loop through each character of the input string
    for (Tcl_Size i = 0; i < length; i++) {
        unsigned char c = str[i];
        switch (c) {
            case '"':
                Tcl_DStringAppend(dsPtr, "\\\"", 2);
                break;
            case '\\':
                Tcl_DStringAppend(dsPtr, "\\\\", 2);
                break;
            case '\b':
                Tcl_DStringAppend(dsPtr, "\\b", 2);
                break;
            case '\f':
                Tcl_DStringAppend(dsPtr, "\\f", 2);
                break;
            case '\n':
                Tcl_DStringAppend(dsPtr, "\\n", 2);
                break;
            case '\r':
                Tcl_DStringAppend(dsPtr, "\\r", 2);
                break;
            case '\t':
                Tcl_DStringAppend(dsPtr, "\\t", 2);
                break;
            default:
                if (c < 32) {
                    Tcl_DStringAppend(dsPtr, "\\u00", 4);
                    char hex[3];
                    sprintf(hex, "%02x", c);
                    Tcl_DStringAppend(dsPtr, hex, 2);
                } else {
                    char tempstr[2];
                    tempstr[0] = c;
                    tempstr[1] = '\0';
                    Tcl_DStringAppend(dsPtr, tempstr, 1);
                }
        }
    }
    return TCL_OK;
}

static int tjson_TreeToJson(Tcl_Interp *interp, cJSON *item, int num_spaces, Tcl_DString *dsPtr) {
    double d;
    switch ((item->type) & 0xFF)
    {
        case cJSON_NULL:
            Tcl_DStringAppend(dsPtr, "null", 4);
            return TCL_OK;
        case cJSON_False:
            Tcl_DStringAppend(dsPtr, "false", 5);
            return TCL_OK;
        case cJSON_True:
            Tcl_DStringAppend(dsPtr, "true", 4);
            return TCL_OK;
        case cJSON_Number:
            d = item->valuedouble;
            if (isnan(d) || isinf(d)) {
                Tcl_DStringAppend(dsPtr, "null", 4);
                return TCL_OK;
            } else if(d == (double)item->valueint) {
                Tcl_Size intstr_length;
                Tcl_Obj *intObjPtr = Tcl_NewIntObj(item->valueint);
                Tcl_IncrRefCount(intObjPtr);
                const char *intstr = Tcl_GetStringFromObj(intObjPtr, &intstr_length);
                Tcl_DStringAppend(dsPtr, intstr, intstr_length);
                Tcl_DecrRefCount(intObjPtr);
                return TCL_OK;
            } else {
                Tcl_Size doublestr_length;
                Tcl_Obj *doubleObjPtr = Tcl_NewDoubleObj(item->valuedouble);
                Tcl_IncrRefCount(doubleObjPtr);
                const char *doublestr = Tcl_GetStringFromObj(doubleObjPtr, &doublestr_length);
                Tcl_DStringAppend(dsPtr, doublestr, doublestr_length);
                Tcl_DecrRefCount(doubleObjPtr);
                return TCL_OK;
            }
        case cJSON_Raw:
        {
            if (item->valuestring == NULL)
            {
                Tcl_DStringAppend(dsPtr, "\"\"", 2);
                return TCL_OK;
            }
            Tcl_DStringAppend(dsPtr, "\"", 1);
            Tcl_Obj *strObjPtr = Tcl_NewStringObj(item->valuestring, -1);
            Tcl_IncrRefCount(strObjPtr);
            tjson_EscapeJsonString(strObjPtr, dsPtr);
            Tcl_DStringAppend(dsPtr, "\"", 1);
            Tcl_DecrRefCount(strObjPtr);
            return TCL_OK;
        }

        case cJSON_String:
            Tcl_DStringAppend(dsPtr, "\"", 1);
            Tcl_Obj *strObjPtr = Tcl_NewStringObj(item->valuestring, -1);
            Tcl_IncrRefCount(strObjPtr);
            tjson_EscapeJsonString(strObjPtr, dsPtr);
            Tcl_DStringAppend(dsPtr, "\"", 1);
            Tcl_DecrRefCount(strObjPtr);
            return TCL_OK;
        case cJSON_Array:
            Tcl_DStringAppend(dsPtr, LBRACKET, 1);
            if (num_spaces) {
                Tcl_DStringAppend(dsPtr, NL, 1);
            }
            cJSON *current_element = item->child;
            int first_array_element = 1;
            while (current_element != NULL) {
                if (first_array_element) {
                    first_array_element = 0;
                } else {
                    Tcl_DStringAppend(dsPtr, COMMA, 1);
                    if (num_spaces) {
                        Tcl_DStringAppend(dsPtr, NL, 1);
                    }
                }
                if (num_spaces) {
                    for (int i = 0; i < num_spaces; i++) {
                        Tcl_DStringAppend(dsPtr, SP, 1);
                    }
                }
                if (TCL_OK != tjson_TreeToJson(interp, current_element, num_spaces > 0 ? num_spaces + 2 : 0, dsPtr)) {
                    return TCL_ERROR;
                }
                current_element = current_element->next;
            }
            if (num_spaces) {
                Tcl_DStringAppend(dsPtr, NL, 1);
                for (int i = 0; i < num_spaces - 2; i++) {
                    Tcl_DStringAppend(dsPtr, SP, 1);
                }
            }
            Tcl_DStringAppend(dsPtr, RBRACKET, 1);
            return TCL_OK;
        case cJSON_Object:
            Tcl_DStringAppend(dsPtr, LBRACE, 1);
            if (num_spaces) {
                Tcl_DStringAppend(dsPtr, NL, 1);
            }
            cJSON *current_item = item->child;
            int first_object_item = 1;
            while (current_item) {
                if (first_object_item) {
                    first_object_item = 0;
                } else {
                    Tcl_DStringAppend(dsPtr, COMMA, 1);
                    if (num_spaces) {
                        Tcl_DStringAppend(dsPtr, NL, 1);
                    }
                }
                if (num_spaces) {
                    for (int i = 0; i < num_spaces; i++) {
                        Tcl_DStringAppend(dsPtr, SP, 1);
                    }
                }
                Tcl_DStringAppend(dsPtr, "\"", 1);
                Tcl_Obj *strToEscapeObjPtr = Tcl_NewStringObj(current_item->string, -1);
                Tcl_IncrRefCount(strToEscapeObjPtr);
                tjson_EscapeJsonString(strToEscapeObjPtr, dsPtr);
                Tcl_DecrRefCount(strToEscapeObjPtr);
                Tcl_DStringAppend(dsPtr, "\":", 2);
                if (num_spaces) {
                    Tcl_DStringAppend(dsPtr, SP, 1);
                }
                if (TCL_OK != tjson_TreeToJson(interp, current_item, num_spaces > 0 ? num_spaces + 2 : 0, dsPtr)) {
                    return TCL_ERROR;
                }
                current_item = current_item->next;
            }
            if (num_spaces) {
                Tcl_DStringAppend(dsPtr, NL, 1);
                for (int i = 0; i < num_spaces - 2; i++) {
                    Tcl_DStringAppend(dsPtr, SP, 1);
                }
            }
            Tcl_DStringAppend(dsPtr, RBRACE, 1);
            return TCL_OK;
        default:
            Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid type", -1));
            return TCL_ERROR;
    }
}

int ttrek_TouchFile(Tcl_Interp *interp, Tcl_Obj *path_ptr) {
    Tcl_Channel chan = Tcl_OpenFileChannel(interp, Tcl_GetString(path_ptr), "w", 0666);
    if (!chan) {
        fprintf(stderr, "error: could not open %s\n", Tcl_GetString(path_ptr));
        return TCL_ERROR;
    }
    Tcl_Close(interp, chan);
    return TCL_OK;
}

int ttrek_InitSpecFile(Tcl_Interp *interp, Tcl_Obj *path_to_spec_ptr, const char *project_name, const char *project_version) {
    cJSON *spec_root = cJSON_CreateObject();
    cJSON *name = cJSON_CreateString(project_name);
    cJSON_AddItemToObject(spec_root, "name", name);
    cJSON *version = cJSON_CreateString(project_version);
    cJSON_AddItemToObject(spec_root, "version", version);
    cJSON *scripts = cJSON_CreateObject();
    cJSON_AddItemToObject(spec_root, "scripts", scripts);
    cJSON *spec_dependencies = cJSON_CreateObject();
    cJSON_AddItemToObject(spec_root, "dependencies", spec_dependencies);
    cJSON *devDependencies = cJSON_CreateObject();
    cJSON_AddItemToObject(spec_root, "devDependencies", devDependencies);
    if (TCL_OK != ttrek_WriteJsonFile(interp, path_to_spec_ptr, spec_root)) {
        cJSON_Delete(spec_root);
        return TCL_ERROR;
    }
    cJSON_Delete(spec_root);
    return TCL_OK;
}

int ttrek_InitLockFile(Tcl_Interp *interp, Tcl_Obj *path_to_lock_ptr) {
    cJSON *lock_root = cJSON_CreateObject();
    cJSON *lock_packages = cJSON_CreateObject();
    cJSON_AddItemToObject(lock_root, "packages", lock_packages);
    cJSON *lock_dependencies = cJSON_CreateObject();
    cJSON_AddItemToObject(lock_root, "dependencies", lock_dependencies);
    if (TCL_OK != ttrek_WriteJsonFile(interp, path_to_lock_ptr, lock_root)) {
        cJSON_Delete(lock_root);
        return TCL_ERROR;
    }
    cJSON_Delete(lock_root);
    return TCL_OK;
}