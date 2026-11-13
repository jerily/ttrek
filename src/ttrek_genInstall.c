/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "common.h"
#include "ttrek_useflags.h"
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

static const char install_script_common_dynamic[] = {
#include "install_common_dynamic.sh.h"

        0x00
};

static const char script_package[] = {
#include "script_package.sh.h"

        0x00
};

static const char install_script_common_static[] = {
#include "install_common_static.sh.h"

        0x00
};

static const char script_header[] = {
#include "script_header.sh.h"

        0x00
};

static const char *pkg_counter_script =
        "\n"
        "PKG_CUR=%s\n"
        "PKG_TOT=%s\n";

static const char *pkg_counter_template = "${%d:-1}";

static int ttrek_IsUseFlagEnabled(Tcl_Interp *interp, Tcl_HashTable *use_flags_ht_ptr,
                                  const cJSON *json, int *result) {

    const cJSON *flagJson = cJSON_GetObjectItem(json, "if");

    if (flagJson != NULL) {

        if (!cJSON_IsString(flagJson)) {
            SetResult("\"if\" property in json is not a string");
            return TCL_ERROR;
        }

        const char *flag_str = cJSON_GetStringValue(flagJson);

        if (flag_str == NULL) {
            SetResult("error while parsing \"if\" property in json");
            return TCL_ERROR;
        }

        if (TCL_OK != ttrek_HashTableContainsUseFlag(interp, use_flags_ht_ptr, flag_str, result)) {
            SetResult("error while checking if a flag exists");
            return TCL_ERROR;
        }

        DBG2(fprintf(stderr, "check flag \"%s\": %s", flag_str, (*result ? "yes" : "no")));
        return TCL_OK;

    }

    // If no flags are defined, consider the flag enabled
    *result = 1;
    return TCL_OK;

}

static Tcl_Obj *ttrek_StringToSingleQuotedObj(const char *str, Tcl_Size len) {
    if (len < 0) {
        len = strlen(str);
    }
    Tcl_Obj *rc = Tcl_NewStringObj("'", 1);
    // How many bytes to copy into the result object
    Tcl_Size tocopy = 0;
    Tcl_Size i;
    for (i = 0; i < len; i++) {
        if (str[i] == '\'') {
            if (tocopy) {
                Tcl_AppendToObj(rc, &str[i - tocopy], tocopy);
                tocopy = 0;
            }
            Tcl_AppendToObj(rc, "'\"'\"'", 5);
        } else {
            tocopy++;
        }
    }
    if (tocopy) {
        Tcl_AppendToObj(rc, &str[i - tocopy], tocopy);
    }
    Tcl_AppendToObj(rc, "'", 1);
    return rc;
}

// WARNING: this function will release the source object if it has zero
// refcount.
static Tcl_Obj *ttrek_ObjectToSingleQuotedObj(Tcl_Obj *obj) {
    Tcl_Size len;
    const char *str = Tcl_GetStringFromObj(obj, &len);
    Tcl_Obj *rc = ttrek_StringToSingleQuotedObj(str, len);
    Tcl_BounceRefCount(obj);
    return rc;
}

static Tcl_Obj *ttrek_StringToDoubleQuotedObj(const char *str, Tcl_Size len) {
    if (len < 0) {
        len = strlen(str);
    }
    Tcl_Obj *rc = Tcl_NewStringObj("\"", 1);
    // How many bytes to copy into the result object
    Tcl_Size tocopy = 0;
    Tcl_Size i;
    for (i = 0; i < len; i++) {
        if (str[i] == '\"' || str[i] == '\\' || str[i] == '`' ||
            (i < (len - 1) && str[i] == '$' && str[i + 1] == '(')) {
            if (tocopy) {
                Tcl_AppendToObj(rc, &str[i - tocopy], tocopy);
                tocopy = 0;
            }
            Tcl_AppendToObj(rc, "\\", 1);
        }
        tocopy++;
    }
    if (tocopy) {
        Tcl_AppendToObj(rc, &str[i - tocopy], tocopy);
    }
    Tcl_AppendToObj(rc, "\"", 1);
    return rc;
}

// WARNING: this function will release the source object if it has zero
// refcount.
static Tcl_Obj *ttrek_ObjectToDoubleQuotedObj(Tcl_Obj *obj) {
    Tcl_Size len;
    const char *str = Tcl_GetStringFromObj(obj, &len);
    Tcl_Obj *rc = ttrek_StringToDoubleQuotedObj(str, len);
    Tcl_BounceRefCount(obj);
    return rc;
}

static int ttrek_ValidateShellVariableName(Tcl_Interp *interp, Tcl_Obj *obj) {

    Tcl_Size len;
    const char *str = Tcl_GetStringFromObj(obj, &len);

    // empty string is invalid shell variable name
    if (len < 1) {
        DBG2(fprintf(stderr, "return false (string is empty)"));
        if (interp != NULL) {
            SetResult("shell variable name cannot be an empty string");
        }
        return 0;
    }

    // Check if the first character is a letter or an underscore
    if (!isalpha(str[0]) && str[0] != '_') {
        DBG2(fprintf(stderr, "return false (first character is invalid)"));
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("the first character in"
                                                   " the variable name \"%s\" is invalid", str));
        }
        return 0;
    }

    // Check the rest of the string
    for (Tcl_Size i = 1; i < len; i++) {
        if (!isalnum(str[i]) && str[i] != '_') {
            DBG2(fprintf(stderr, "return false (character at index %" TCL_SIZE_MODIFIER
                         "d is invalid)", i));
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("character at index %"
                                                       TCL_SIZE_MODIFIER "d in the variable name \"%s\" is invalid",
                                                       i, str));
            }
            return 0;
        }
    }

    return 1;

}

static Tcl_Obj *ttrek_cJSONStringToObject(const cJSON *json, const char *key) {
    const cJSON *obj = cJSON_GetObjectItem(json, key);
    if (obj == NULL || !cJSON_IsString(obj)) {
        return NULL;
    }
    return Tcl_NewStringObj(cJSON_GetStringValue(obj), -1);
}

/*
static Tcl_Obj *ttrek_cJSONNumberToObject(const cJSON *json, const char *key) {
    const cJSON *obj = cJSON_GetObjectItem(json, key);
    if (obj == NULL || !cJSON_IsNumber(obj)) {
        return NULL;
    }
    return Tcl_NewIntObj(cJSON_GetNumberValue(obj));
}
*/

// WARNING: this function will release format objects if their refcount is zero.
static Tcl_Obj *ttrek_AppendFormatToObj(Tcl_Interp *interp, Tcl_Obj *result, const char *format,
                                        int objc, ...) {
    int i;
    Tcl_Obj *objs[30];
    va_list valist;
    va_start(valist, objc);
    for (i = 0; i < objc; i++) {
        objs[i] = va_arg(valist, Tcl_Obj *);
    }
    va_end(valist);
    if (result == NULL) {
        result = Tcl_NewObj();
    }
    Tcl_AppendFormatToObj(interp, result, format, objc, objs);
    for (i = 0; i < objc; i++) {
        Tcl_BounceRefCount(objs[i]);
    }
    return result;
}

static void ttrek_SpecToObj_AppendCommand(Tcl_Interp *interp, Tcl_Obj *resultList, Tcl_Obj *cmd, Tcl_Obj *log_file) {
    if (log_file != NULL) {
        DBG2(fprintf(stderr, "new cmd: [%s]; log file: [%s]", Tcl_GetString(cmd), Tcl_GetString(log_file)));
        Tcl_AppendToObj(cmd, " >", 2);
        Tcl_AppendObjToObj(cmd, log_file);
        Tcl_AppendToObj(cmd, " 2>&1", 5);
    } else {
        DBG2(fprintf(stderr, "new cmd: [%s]", Tcl_GetString(cmd)));
    }
    Tcl_AppendToObj(cmd, " || fail", -1);
    if (log_file != NULL) {
        Tcl_AppendToObj(cmd, " ", 1);
        Tcl_AppendObjToObj(cmd, log_file);
        Tcl_BounceRefCount(log_file);
    }
    Tcl_ListObjAppendElement(interp, resultList, cmd);
}

#define sq(x) ttrek_StringToSingleQuotedObj(x, -1)
#define dq(x) ttrek_StringToDoubleQuotedObj(x, -1)
#define osq(x) ttrek_ObjectToSingleQuotedObj(x)
#define odq(x) ttrek_ObjectToDoubleQuotedObj(x)

#define APPEND_CMD(x, y) ttrek_SpecToObj_AppendCommand(interp, resultList, (x), (y));

#define DEFINE_COMMAND(x) static int ttrek_SpecToObj_##x(Tcl_Interp *interp, ttrek_state_t *state_ptr, const cJSON *opts, Tcl_HashTable *use_flags_ht_ptr, Tcl_Obj *resultList)

DEFINE_COMMAND(NpmInstall) {

    UNUSED(use_flags_ht_ptr);
    UNUSED(state_ptr);

    Tcl_Obj *cmd;

    cmd = Tcl_NewStringObj("_NPM_SAVE_PWD=\"$(pwd)\" && cd \"$PROJECT_HOME\"", -1);
    APPEND_CMD(cmd, NULL);

    cmd = Tcl_NewStringObj("[ -f package.json -a -d node_modules ] || npm init -y", -1);

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/npm_init.log"));

    cmd = Tcl_NewStringObj("npm install", -1);

    const cJSON *packages = cJSON_GetObjectItem(opts, "packages");
    if (packages == NULL) {
        goto skip_packages;
    }

    if (!cJSON_IsArray(packages)) {
        Tcl_BounceRefCount(cmd);
        SetResult("the package property is not an array");
        return TCL_ERROR;
    }

    const cJSON *package;
    cJSON_ArrayForEach(package, packages) {

        if (!cJSON_IsString(package)) {
            Tcl_BounceRefCount(cmd);
            SetResult("element in the package property does not have a string type");
            return TCL_ERROR;
        }

        ttrek_AppendFormatToObj(interp, cmd, " %s", 1, sq(cJSON_GetStringValue(package)));

    }


skip_packages:

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/npm_install.log"));

    cmd = Tcl_NewStringObj("cd \"$_NPM_SAVE_PWD\"", -1);
    APPEND_CMD(cmd, NULL);

    return TCL_OK;

}

DEFINE_COMMAND(EnvVariable) {

    UNUSED(use_flags_ht_ptr);
    UNUSED(state_ptr);

    Tcl_Obj *cmd;

    static const char *const op_modes[] = {
            "set", "append", "prepend", "unset",
            NULL
    };

    enum op_modes {
        OP_ENV_SET, OP_ENV_APPEND, OP_ENV_PREPEND, OP_ENV_UNSET
    };

    int op_mode;

    Tcl_Obj *op_mode_ptr = ttrek_cJSONStringToObject(opts, "op");
    if (op_mode_ptr == NULL) {
        // The default mode is: append
        op_mode = OP_ENV_APPEND;
    } else {
        int res = Tcl_GetIndexFromObj(interp, op_mode_ptr, op_modes,
                                      "operation mode for environment variable", 0, &op_mode);
        Tcl_BounceRefCount(op_mode_ptr);
        if (res != TCL_OK) {
            return TCL_ERROR;
        }
    }

    Tcl_Obj *name = ttrek_cJSONStringToObject(opts, "name");
    if (name == NULL) {
        SetResult("error while parsing \"env_variable\" cmd: no name or name"
                  " is not a string");
        return TCL_ERROR;
    }

    if (!ttrek_ValidateShellVariableName(interp, name)) {
        Tcl_BounceRefCount(name);
        return TCL_ERROR;
    }

    if (op_mode == OP_ENV_UNSET) {

        cmd = ttrek_AppendFormatToObj(interp, NULL, "unset %s", 1, name);
        APPEND_CMD(cmd, NULL);

    } else {

        // Make sure we have a value for set/application modes of operations.
        Tcl_Obj *value = ttrek_cJSONStringToObject(opts, "value");
        if (value == NULL) {
            SetResult("error while parsing \"env_variable\" cmd: no value or value"
                      " is not a string for set/append/prepend operation");
            Tcl_BounceRefCount(name);
            return TCL_ERROR;
        }

        // increment the reference count for the name variable, since we are using it twice
        Tcl_IncrRefCount(name);

        if (op_mode == OP_ENV_SET) {
            cmd = ttrek_AppendFormatToObj(interp, NULL, "%s=%s", 2, name, odq(value));
        } else {
            // op_mode == OP_ENV_APPEND or OP_ENV_PREPEND
            Tcl_Obj *var_copy = Tcl_ObjPrintf("${%s}", Tcl_GetString(name));
            if (op_mode == OP_ENV_APPEND) {
                cmd = ttrek_AppendFormatToObj(interp, NULL, "%s=%s' '%s", 3, name,
                                              odq(var_copy), odq(value));
            } else {
                // op_mode == OP_ENV_PREPEND
                cmd = ttrek_AppendFormatToObj(interp, NULL, "%s=%s' '%s", 3, name,
                                              odq(value), odq(var_copy));
            }
        }

        APPEND_CMD(cmd, NULL);

        cmd = ttrek_AppendFormatToObj(interp, NULL, "export %s", 1, name);
        APPEND_CMD(cmd, NULL);

        Tcl_DecrRefCount(name);

    }

    return TCL_OK;

}

/* Archive names and extraction destinations are literal relative paths. */
static int ttrek_RelativePathValid(const char *path) {
    if (!path || !*path || *path == '/' || strchr(path, '\\') || strchr(path, ':')) {
        return 0;
    }
    const char *part = path;
    do {
        const char *end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if (!length || (length == 2 && strncmp(part, "..", 2) == 0)) { return 0; }
        if (!end) { break; }
        part = end + 1;
    } while (1);
    return 1;
}

static Tcl_Obj *ttrek_ArchivePath(Tcl_Interp *interp, const cJSON *opts) {
    const cJSON *filename = cJSON_GetObjectItem(opts, "filename");
    if (!filename) { return dq("$DOWNLOAD_DIR/$ARCHIVE_FILE"); }
    if (!cJSON_IsString(filename) || !ttrek_RelativePathValid(filename->valuestring) ||
        strcmp(filename->valuestring, ".") == 0) {
        SetResult("archive filename must be a relative path without '..'");
        return NULL;
    }
    return ttrek_AppendFormatToObj(interp, NULL, "%s/%s", 2,
                                 dq("$DOWNLOAD_DIR"), sq(filename->valuestring));
}

DEFINE_COMMAND(Download) {
    UNUSED(use_flags_ht_ptr);
    const cJSON *url = cJSON_GetObjectItem(opts, "url");
    if (!cJSON_IsString(url) || !*url->valuestring) {
        SetResult("download requires a nonempty URL string");
        return TCL_ERROR;
    }
    const cJSON *filename = cJSON_GetObjectItem(opts, "filename");
    Tcl_Obj *path = ttrek_ArchivePath(interp, opts);
    if (!path) { return TCL_ERROR; }
    Tcl_IncrRefCount(path);
    Tcl_Obj *cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd mkdir -p \"$(dirname %s)\"", 1, path);
    APPEND_CMD(cmd, NULL);
    // Publish downloads atomically: another package may be reading the same
    // named archive while this package downloads it again.
    cmd = ttrek_AppendFormatToObj(interp, NULL,
        "( TTREK_DOWNLOAD_FILE=$(mktemp %s.XXXXXX) || exit; "
        "trap 'rm -f \"$TTREK_DOWNLOAD_FILE\"' 0; ", 1, path);
    if (state_ptr->mode == MODE_SCRIPT) {
        Tcl_Obj *url_obj = Tcl_NewStringObj(url->valuestring, -1);
        Tcl_Obj *fallback = ttrek_DownloadFallbackUrl(url_obj, "$PACKAGE-$VERSION",
            filename ? filename->valuestring : NULL);
        Tcl_BounceRefCount(url_obj);
        if (!fallback) {
            Tcl_BounceRefCount(cmd);
            Tcl_DecrRefCount(path);
            SetResult("could not encode fallback download URL");
            return TCL_ERROR;
        }
        ttrek_AppendFormatToObj(interp, cmd,
            "{ cmd curl -fLsS --retry 3 -o %s %s || cmd curl -fLsS --retry 3 -o %s %s; }",
            4, dq("$TTREK_DOWNLOAD_FILE"), sq(url->valuestring), dq("$TTREK_DOWNLOAD_FILE"), odq(fallback));
    } else {
        ttrek_AppendFormatToObj(interp, cmd, "cmd %s download %s %s %s", 4,
                                      sq(Tcl_GetNameOfExecutable()), sq(url->valuestring),
                                      dq("$TTREK_DOWNLOAD_FILE"), dq("$PACKAGE-$VERSION"));
        if (filename) {
            ttrek_AppendFormatToObj(interp, cmd, " %s", 1, sq(filename->valuestring));
        }
    }
    ttrek_AppendFormatToObj(interp, cmd,
        " || exit; cmd mv -f \"$TTREK_DOWNLOAD_FILE\" %s )", 1, path);
    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/download.log"));
    Tcl_DecrRefCount(path);
    return TCL_OK;
}

DEFINE_COMMAND(Patch) {

    UNUSED(use_flags_ht_ptr);
    UNUSED(state_ptr);

    Tcl_Obj *cmd;

    Tcl_Obj *filename = ttrek_cJSONStringToObject(opts, "filename");
    if (filename == NULL) {
        SetResult("error while parsing \"patch\" cmd: no filename or filename is not a string");
        return TCL_ERROR;
    }

    // The filename variable will be used 2 times in this command. We need
    // to increase refcount so that we don't release it after the first use.
    Tcl_IncrRefCount(filename);

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cd %s", 1, dq("$SOURCE_DIR"));

    APPEND_CMD(cmd, NULL);

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cat %s/patch-%s-%s-%s | cmd patch", 4,
                                  dq("$PATCH_DIR"), dq("$PACKAGE"), dq("$VERSION"), osq(filename));

    Tcl_Obj *p_num = ttrek_cJSONStringToObject(opts, "p_num");
    if (p_num != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, " -p%s", 1, osq(p_num));
    }

    Tcl_Obj *log_file = ttrek_AppendFormatToObj(interp, NULL, "%s/patch-%s.log", 2,
                                                dq("$BUILD_LOG_DIR"), osq(filename));

    APPEND_CMD(cmd, log_file);

    Tcl_DecrRefCount(filename);

    return TCL_OK;

}

DEFINE_COMMAND(Git) {

    UNUSED(use_flags_ht_ptr);
    UNUSED(state_ptr);

    Tcl_Obj *cmd;

    Tcl_Obj *url = ttrek_cJSONStringToObject(opts, "url");
    if (url == NULL) {
        SetResult("error while parsing \"git\" cmd: no url or url is not a string");
        return TCL_ERROR;
    }

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd git -C %s clone %s --depth 1"
                                                " --single-branch", 2, dq("$SOURCE_DIR"), osq(url));

    Tcl_Obj *branch = ttrek_cJSONStringToObject(opts, "branch");
    if (branch != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, " --branch %s", 1, osq(branch));
    }

    const cJSON *submodules_recurse = cJSON_GetObjectItem(opts, "recurse-submodules");
    if (submodules_recurse != NULL && cJSON_IsTrue(submodules_recurse)) {
        Tcl_AppendToObj(cmd, " --recurse-submodules", -1);
    }

    const cJSON *submodules_shallow = cJSON_GetObjectItem(opts, "shallow-submodules");
    if (submodules_shallow != NULL && cJSON_IsTrue(submodules_shallow)) {
        Tcl_AppendToObj(cmd, " --shallow-submodules", -1);
    }

    Tcl_AppendToObj(cmd, " .", -1);

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/download.log"));

    cmd = ttrek_AppendFormatToObj(interp, NULL, "find %s -name '.git' -print0 |"
                                                " xargs -0 rm -rf", 1, dq("$SOURCE_DIR"));

    APPEND_CMD(cmd, NULL);

    return TCL_OK;

}

static Tcl_Obj *ttrek_SourceVariable(const char *name) {
    return Tcl_ObjPrintf("_TTREK_SOURCE_%s", name);
}

static Tcl_Obj *ttrek_SourcePath(const char *name) {
    return odq(Tcl_ObjPrintf("${_TTREK_SOURCE_%s}", name));
}

DEFINE_COMMAND(Unpack) {
    UNUSED(use_flags_ht_ptr);
    const cJSON *destination = cJSON_GetObjectItem(opts, "destination");
    if (destination && (!cJSON_IsString(destination) || !ttrek_RelativePathValid(destination->valuestring))) {
        SetResult("unpack destination must be a relative path without '..'");
        return TCL_ERROR;
    }
    Tcl_Obj *archive = ttrek_ArchivePath(interp, opts);
    if (!archive) { return TCL_ERROR; }
    const cJSON *alias = cJSON_GetObjectItem(opts, "as");
    const cJSON *into = cJSON_GetObjectItem(opts, "into");
    const cJSON *shared = cJSON_GetObjectItem(opts, "shared");
    const cJSON *select = cJSON_GetObjectItem(opts, "select");
    if (shared) {
        Tcl_Obj *cmd = ttrek_AppendFormatToObj(interp, NULL,
            "%s=$(cmd shared_unpack %s %s %s unpack)", 4,
            ttrek_SourceVariable(alias->valuestring), archive, sq(shared->valuestring),
            state_ptr->mode == MODE_SCRIPT ? Tcl_NewObj() : sq(Tcl_GetNameOfExecutable()));
        APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/unpack.log"));
        cmd = ttrek_AppendFormatToObj(interp, NULL, "readonly %s", 1,
                                    ttrek_SourceVariable(alias->valuestring));
        APPEND_CMD(cmd, NULL);
        if (cJSON_IsTrue(select)) {
            cmd = ttrek_AppendFormatToObj(interp, NULL, "SOURCE_DIR=%s", 1,
                                        ttrek_SourcePath(alias->valuestring));
            APPEND_CMD(cmd, NULL);
        }
        return TCL_OK;
    }
    Tcl_IncrRefCount(archive);
    if (alias) {
        Tcl_Obj *cmd = ttrek_AppendFormatToObj(interp, NULL,
            "%s=%s/%s", 3, ttrek_SourceVariable(alias->valuestring),
            dq("$ROOT_BUILD_DIR/named-source/${PACKAGE}-${VERSION}"), sq(alias->valuestring));
        APPEND_CMD(cmd, NULL);
        cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd rm -rf %s", 1,
                                    ttrek_SourcePath(alias->valuestring));
        APPEND_CMD(cmd, NULL);
    }
    // In named-source recipes, unnamed unpacks retain the original package
    // root even after selecting a named tree. Legacy recipes use SOURCE_DIR.
    Tcl_Obj *root = alias ? ttrek_SourcePath(alias->valuestring)
        : into ? ttrek_SourcePath(into->valuestring)
        : dq("${_TTREK_UNPACK_ROOT:-$SOURCE_DIR}");
    Tcl_Obj *path = destination
        ? ttrek_AppendFormatToObj(interp, NULL, "%s/%s", 2, root, sq(destination->valuestring))
        : root;
    Tcl_IncrRefCount(path);
    Tcl_Obj *cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd mkdir -p %s", 1, path);
    APPEND_CMD(cmd, NULL);
    if (state_ptr->mode == MODE_SCRIPT) {
        cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd unpack %s %s", 2, archive, path);
    } else {
        cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd %s unpack %s %s", 3,
                                      sq(Tcl_GetNameOfExecutable()), archive, path);
    }
    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/unpack.log"));
    if (alias) {
        cmd = ttrek_AppendFormatToObj(interp, NULL, "readonly %s", 1,
                                    ttrek_SourceVariable(alias->valuestring));
        APPEND_CMD(cmd, NULL);
    }
    if (cJSON_IsTrue(select)) {
        cmd = ttrek_AppendFormatToObj(interp, NULL, "SOURCE_DIR=%s", 1,
                                    ttrek_SourcePath(alias ? alias->valuestring : into->valuestring));
        APPEND_CMD(cmd, NULL);
    }
    Tcl_DecrRefCount(archive);
    Tcl_DecrRefCount(path);
    return TCL_OK;
}

DEFINE_COMMAND(Cd) {

    UNUSED(use_flags_ht_ptr);
    UNUSED(state_ptr);

    Tcl_Obj *cmd;

    Tcl_Obj *dirname = ttrek_cJSONStringToObject(opts, "dirname");

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd cd %s", 1,
                                  (dirname == NULL ? dq("$BUILD_DIR") : odq(dirname)));

    APPEND_CMD(cmd, NULL);

    return TCL_OK;

}

DEFINE_COMMAND(Autogen) {

    UNUSED(state_ptr);

    Tcl_Obj *cmd;

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd cd %s", 1, dq("$SOURCE_DIR"));

    APPEND_CMD(cmd, NULL);

    cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONStringToObject(opts, "ld_library_path");
    if (ld_library_path != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "LD_LIBRARY_PATH=%s ", 1,
                                odq(ld_library_path));
    }

    Tcl_Obj *path = ttrek_cJSONStringToObject(opts, "path");
    if (path == NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "cmd %s", 1, dq("./autogen.sh"));
    } else {
        // TODO: make sure the path is under $SOURCE_DIR
        ttrek_AppendFormatToObj(interp, cmd, "cmd %s", 1, odq(path));
    }

    const cJSON *options = cJSON_GetObjectItem(opts, "options");
    if (options == NULL) {
        goto skip_options;
    }

    Tcl_Obj *option_prefix = ttrek_cJSONStringToObject(opts, "option_prefix");
    if (option_prefix == NULL) {
        option_prefix = Tcl_NewStringObj("--", 2);
    }
    Tcl_IncrRefCount(option_prefix);

    const cJSON *option;
    cJSON_ArrayForEach(option, options) {

        int is_continue;
        if (ttrek_IsUseFlagEnabled(interp, use_flags_ht_ptr, option, &is_continue) != TCL_OK) {
            Tcl_DecrRefCount(option_prefix);
            Tcl_BounceRefCount(cmd);
            return TCL_ERROR;
        }
        if (!is_continue) {
            continue;
        }

        Tcl_Obj *name = ttrek_cJSONStringToObject(option, "name");
        if (name == NULL) {
            continue;
        }

        Tcl_Obj *value = ttrek_cJSONStringToObject(option, "value");

        if (value == NULL) {
            ttrek_AppendFormatToObj(interp, cmd, " %s", 1, odq(name));
        } else {
            ttrek_AppendFormatToObj(interp, cmd, " %s%s=%s", 3, osq(option_prefix),
                                    osq(name), odq(value));
        }

    }

    Tcl_DecrRefCount(option_prefix);

    skip_options:

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/autogen.log"));

    return TCL_OK;

}

DEFINE_COMMAND(Configure) {

    UNUSED(state_ptr);

    Tcl_Obj *cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONStringToObject(opts, "ld_library_path");
    if (ld_library_path != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "LD_LIBRARY_PATH=%s ", 1,
                                odq(ld_library_path));
    }

    Tcl_Obj *path = ttrek_cJSONStringToObject(opts, "path");
    if (path == NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "cmd %s", 1, dq("$SOURCE_DIR/configure"));
    } else {
        // TODO: make sure the path is under $SOURCE_DIR
        ttrek_AppendFormatToObj(interp, cmd, "cmd %s", 1, odq(path));
    }

    Tcl_Obj *cmd_option_prefix = ttrek_cJSONStringToObject(opts, "option_prefix");
    if (cmd_option_prefix == NULL) {
        cmd_option_prefix = Tcl_NewStringObj("--", 2);
    }
    Tcl_IncrRefCount(cmd_option_prefix);

    ttrek_AppendFormatToObj(interp, cmd, " %sprefix=%s", 2, osq(cmd_option_prefix),
                            dq("$INSTALL_DIR"));

    const cJSON *options = cJSON_GetObjectItem(opts, "options");
    if (options == NULL) {
        goto skip_options;
    }

    const cJSON *option;
    cJSON_ArrayForEach(option, options) {

        int is_continue;
        if (ttrek_IsUseFlagEnabled(interp, use_flags_ht_ptr, option, &is_continue) != TCL_OK) {
            Tcl_DecrRefCount(cmd_option_prefix);
            Tcl_BounceRefCount(cmd);
            return TCL_ERROR;
        }
        if (!is_continue) {
            continue;
        }

        Tcl_Obj *name = ttrek_cJSONStringToObject(option, "name");
        if (name == NULL) {
            continue;
        }

        Tcl_Obj *value = ttrek_cJSONStringToObject(option, "value");

        Tcl_Obj *option_prefix = ttrek_cJSONStringToObject(option, "option_prefix");
        if (option_prefix == NULL) {
            option_prefix = Tcl_NewStringObj("--", 2);
        }
        Tcl_IncrRefCount(option_prefix);

        if (value == NULL) {
            ttrek_AppendFormatToObj(interp, cmd, " %s%s", 2, osq(option_prefix), odq(name));
        } else {
            ttrek_AppendFormatToObj(interp, cmd, " %s%s=%s", 3, osq(option_prefix),
                                    osq(name), odq(value));
        }

        Tcl_DecrRefCount(option_prefix);
    }

    skip_options:

    Tcl_DecrRefCount(cmd_option_prefix);

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/configure.log"));

    return TCL_OK;

}

DEFINE_COMMAND(CmakeConfig) {

    UNUSED(state_ptr);

    Tcl_Obj *cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONStringToObject(opts, "ld_library_path");
    if (ld_library_path != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "LD_LIBRARY_PATH=%s ", 1,
                                odq(ld_library_path));
    }

    Tcl_Obj *source = ttrek_cJSONStringToObject(opts, "source");
    ttrek_AppendFormatToObj(interp, cmd, "cmd cmake %s", 1,
                            source ? odq(source) : dq("$SOURCE_DIR"));

    ttrek_AppendFormatToObj(interp, cmd, " -DCMAKE_INSTALL_PREFIX=%s", 1,
                            dq("$INSTALL_DIR"));

    ttrek_AppendFormatToObj(interp, cmd, " -DCMAKE_PREFIX_PATH=%s", 1,
                            dq("$INSTALL_DIR/"));

    const cJSON *options = cJSON_GetObjectItem(opts, "options");
    if (options == NULL) {
        goto skip_options;
    }

    const cJSON *option;
    cJSON_ArrayForEach(option, options) {

        int is_continue;
        if (ttrek_IsUseFlagEnabled(interp, use_flags_ht_ptr, option, &is_continue) != TCL_OK) {
            Tcl_BounceRefCount(cmd);
            return TCL_ERROR;
        }
        if (!is_continue) {
            continue;
        }

        Tcl_Obj *name = ttrek_cJSONStringToObject(option, "name");
        if (name == NULL) {
            continue;
        }

        Tcl_Obj *value = ttrek_cJSONStringToObject(option, "value");

        if (value == NULL) {
            ttrek_AppendFormatToObj(interp, cmd, " %s", 1, odq(name));
        } else {
            ttrek_AppendFormatToObj(interp, cmd, " -D%s=%s", 2, osq(name), odq(value));
        }

    }

    skip_options:

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/configure.log"));

    return TCL_OK;

}

DEFINE_COMMAND(Make) {

    UNUSED(state_ptr);

    Tcl_Obj *cmd = Tcl_NewStringObj("cmd make", -1);

    const cJSON *parallel = cJSON_GetObjectItem(opts, "parallel");
    if (parallel != NULL) {
        if (cJSON_IsNumber(parallel)) {
            ttrek_AppendFormatToObj(interp, cmd, " -j%s", 1,
                                    osq(Tcl_NewIntObj(cJSON_GetNumberValue(parallel))));
        } else if (cJSON_IsBool(parallel)) {
            if (cJSON_IsTrue(parallel)) {
                ttrek_AppendFormatToObj(interp, cmd, " -j%s", 1,
                                        dq("$DEFAULT_THREADS"));
            }
        } else {
            Tcl_BounceRefCount(cmd);
            SetResult("error while parsing \"make\" cmd: option \"parallel\""
                      " must be a boolean or a number");
            return TCL_ERROR;
        }
    }

    const cJSON *options = cJSON_GetObjectItem(opts, "options");
    if (options == NULL) {
        goto skip_options;
    }

    const cJSON *option;
    cJSON_ArrayForEach(option, options) {

        int is_continue;
        if (ttrek_IsUseFlagEnabled(interp, use_flags_ht_ptr, option, &is_continue) != TCL_OK) {
            Tcl_BounceRefCount(cmd);
            return TCL_ERROR;
        }
        if (!is_continue) {
            continue;
        }

        Tcl_Obj *name = ttrek_cJSONStringToObject(option, "name");
        if (name == NULL) {
            continue;
        }

        Tcl_Obj *value = ttrek_cJSONStringToObject(option, "value");

        if (value == NULL) {
            ttrek_AppendFormatToObj(interp, cmd, " %s", 1, odq(name));
        } else {
            ttrek_AppendFormatToObj(interp, cmd, " %s=%s", 2, odq(name), odq(value));
        }

    }

    skip_options:

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/build.log"));

    return TCL_OK;

}

DEFINE_COMMAND(CmakeMake) {

    UNUSED(use_flags_ht_ptr);
    UNUSED(state_ptr);

    Tcl_Obj *cmd;

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd cmake --build %s", 1,
                                  dq("$BUILD_DIR"));

    const cJSON *parallel = cJSON_GetObjectItem(opts, "parallel");
    if (parallel != NULL) {
        if (cJSON_IsNumber(parallel)) {
            ttrek_AppendFormatToObj(interp, cmd, " --parallel %s", 1,
                                    osq(Tcl_NewIntObj(cJSON_GetNumberValue(parallel))));
        } else if (cJSON_IsBool(parallel)) {
            if (cJSON_IsTrue(parallel)) {
                ttrek_AppendFormatToObj(interp, cmd, " --parallel %s", 1,
                                        dq("$DEFAULT_THREADS"));
            }
        } else {
            Tcl_BounceRefCount(cmd);
            SetResult("error while parsing \"cmake_make\" cmd: option \"parallel\""
                      " must be a boolean or a number");
            return TCL_ERROR;
        }
    }

    Tcl_Obj *config = ttrek_cJSONStringToObject(opts, "config");
    if (config != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, " --config=%s", 1, osq(config));
    }

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/build.log"));

    return TCL_OK;

}

DEFINE_COMMAND(MakeInstall) {

    UNUSED(state_ptr);

    Tcl_Obj *cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONStringToObject(opts, "ld_library_path");
    if (ld_library_path != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "LD_LIBRARY_PATH=%s ", 1,
                                odq(ld_library_path));
    }
    Tcl_Obj *target = ttrek_cJSONStringToObject(opts, "target");
    if (target == NULL) {
        Tcl_AppendToObj(cmd, "cmd make install", -1);
    } else {
        ttrek_AppendFormatToObj(interp, cmd, "cmd make %s", 1, odq(target));
    }


    const cJSON *options = cJSON_GetObjectItem(opts, "options");
    if (options == NULL) {
        goto skip_options;
    }

    const cJSON *option;
    cJSON_ArrayForEach(option, options) {

        int is_continue;
        if (ttrek_IsUseFlagEnabled(interp, use_flags_ht_ptr, option, &is_continue) != TCL_OK) {
            Tcl_BounceRefCount(cmd);
            return TCL_ERROR;
        }
        if (!is_continue) {
            continue;
        }

        Tcl_Obj *name = ttrek_cJSONStringToObject(option, "name");
        if (name == NULL) {
            continue;
        }

        Tcl_Obj *value = ttrek_cJSONStringToObject(option, "value");

        if (value == NULL) {
            ttrek_AppendFormatToObj(interp, cmd, " %s", 1, odq(name));
        } else {
            ttrek_AppendFormatToObj(interp, cmd, " %s=%s", 2, odq(name), odq(value));
        }

    }

    skip_options:

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/install.log"));

    return TCL_OK;

}

DEFINE_COMMAND(CmakeInstall) {

    UNUSED(use_flags_ht_ptr);
    UNUSED(state_ptr);

    Tcl_Obj *cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd cmake --install %s", 1,
                                         dq("$BUILD_DIR"));

    Tcl_Obj *config = ttrek_cJSONStringToObject(opts, "config");
    if (config != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, " --config=%s", 1, osq(config));
    }

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/install.log"));

    return TCL_OK;

}


/* Validate only enabled dependency steps; local builds skip source retrieval. */
static int ttrek_IsRecipeCommand(const cJSON *cmd, const char *name) {
    // The dispatcher accepts unambiguous command abbreviations too.
    return cJSON_IsString(cmd) && *cmd->valuestring &&
        strncmp(name, cmd->valuestring, strlen(cmd->valuestring)) == 0;
}

static int ttrek_SourceNameValid(const cJSON *name) {
    return cJSON_IsString(name) && *name->valuestring && strlen(name->valuestring) <= 64 &&
        strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_", *name->valuestring) &&
        strspn(name->valuestring, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == strlen(name->valuestring);
}

static int ttrek_ValidateSources(Tcl_Interp *interp, const cJSON *spec,
                                Tcl_HashTable *flags, int *named_sources) {
    const cJSON *step;
    *named_sources = 0;
    // Detect the mode first, including declarations later in the recipe, so
    // earlier SOURCE_DIR overrides cannot escape named-source validation.
    cJSON_ArrayForEach(step, spec) {
        int enabled;
        if (ttrek_IsUseFlagEnabled(interp, flags, step, &enabled) != TCL_OK) { return TCL_ERROR; }
        if (!enabled) { continue; }
        const cJSON *cmd = cJSON_GetObjectItem(step, "cmd");
        if (ttrek_IsRecipeCommand(cmd, "unpack") &&
            (cJSON_HasObjectItem(step, "as") || cJSON_HasObjectItem(step, "into") ||
             cJSON_HasObjectItem(step, "shared") || cJSON_HasObjectItem(step, "select"))) {
            *named_sources = 1;
        }
    }
    if (!*named_sources) { return TCL_OK; }

    Tcl_HashTable sources;
    Tcl_InitHashTable(&sources, TCL_STRING_KEYS);
    int selected_shared = 0;
    int status = TCL_ERROR;
    cJSON_ArrayForEach(step, spec) {
        int enabled;
        if (ttrek_IsUseFlagEnabled(interp, flags, step, &enabled) != TCL_OK) { goto done; }
        if (!enabled) { continue; }
        const cJSON *cmd = cJSON_GetObjectItem(step, "cmd");
        if (ttrek_IsRecipeCommand(cmd, "unpack")) {
            const cJSON *alias = cJSON_GetObjectItem(step, "as");
            const cJSON *into = cJSON_GetObjectItem(step, "into");
            const cJSON *shared = cJSON_GetObjectItem(step, "shared");
            const cJSON *select = cJSON_GetObjectItem(step, "select");
            if (select && (!cJSON_IsBool(select) || (!alias && !into))) {
                SetResult("unpack select must be a boolean and requires as or into");
                goto done;
            }
            if ((alias && !ttrek_SourceNameValid(alias)) || (into && !ttrek_SourceNameValid(into))) {
                SetResult("source names must have 1-64 letters, digits or underscores, starting with a letter or underscore");
                goto done;
            }
            if (alias && (into || cJSON_HasObjectItem(step, "destination"))) {
                SetResult("unpack as cannot be combined with into or destination; use a later unpack into the named source");
                goto done;
            }
            if (shared && (!alias || into)) {
                SetResult("shared unpack requires as and cannot specify into");
                goto done;
            }
            if (shared && (!cJSON_IsString(shared) || !*shared->valuestring || strlen(shared->valuestring) > 128 ||
                strspn(shared->valuestring, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-") != strlen(shared->valuestring) ||
                !strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", *shared->valuestring))) {
                SetResult("unpack shared must be a key of 1-128 letters, digits, '.', '_', '+', '-', starting with a letter or digit");
                goto done;
            }
            if (alias) {
                int is_new;
                Tcl_HashEntry *entry = Tcl_CreateHashEntry(&sources, alias->valuestring, &is_new);
                if (!is_new) {
                    Tcl_SetObjResult(interp, Tcl_ObjPrintf("source name already declared: %s", alias->valuestring));
                    goto done;
                }
                Tcl_SetHashValue(entry, INT2PTR(shared != NULL));
            }
            if (into) {
                Tcl_HashEntry *entry = Tcl_FindHashEntry(&sources, into->valuestring);
                if (!entry) {
                    Tcl_SetObjResult(interp, Tcl_ObjPrintf("source must be declared before unpack into: %s", into->valuestring));
                    goto done;
                }
                if (PTR2INT(Tcl_GetHashValue(entry))) {
                    Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot unpack into shared source: %s", into->valuestring));
                    goto done;
                }
            }
            if (cJSON_IsTrue(select)) {
                selected_shared = shared != NULL;
            }
        } else if (selected_shared && (ttrek_IsRecipeCommand(cmd, "patch") ||
                   ttrek_IsRecipeCommand(cmd, "git") || ttrek_IsRecipeCommand(cmd, "autogen"))) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("selected source is shared and read-only; cannot use %s", cmd->valuestring));
            goto done;
        } else if (ttrek_IsRecipeCommand(cmd, "env_variable")) {
            const cJSON *name = cJSON_GetObjectItem(step, "name");
            if (cJSON_IsString(name) && (strcmp(name->valuestring, "SOURCE_DIR") == 0 ||
                strcmp(name->valuestring, "_TTREK_UNPACK_ROOT") == 0 ||
                strncmp(name->valuestring, "_TTREK_SOURCE_", 14) == 0)) {
                SetResult("named sources cannot override source variables; use unpack select");
                goto done;
            }
        }
    }
    status = TCL_OK;
done:
    Tcl_DeleteHashTable(&sources);
    return status;
}

static Tcl_Obj *
ttrek_SpecToObj(Tcl_Interp *interp, ttrek_state_t *state_ptr, cJSON *spec, Tcl_HashTable *global_use_flags_ht_ptr,
                int is_local_build) {

    static const struct {
        const char *cmd;
        const char *stage;
        int enable_in_local_build;

        int (*handler)(Tcl_Interp *interp, ttrek_state_t *state_ptr, const cJSON *opts, Tcl_HashTable *use_flags_ht_ptr,
                       Tcl_Obj *resultList);
    } commands[] = {
            {"download",      "1", 0, ttrek_SpecToObj_Download},
            {"git",           "1", 0, ttrek_SpecToObj_Git},
            {"unpack",        "1", 0, ttrek_SpecToObj_Unpack},
            {"patch",         "1", 0, ttrek_SpecToObj_Patch},
            {"npm_install",   "2", 1, ttrek_SpecToObj_NpmInstall},
            {"cd",           NULL, 1, ttrek_SpecToObj_Cd},
            {"env_variable", NULL, 1, ttrek_SpecToObj_EnvVariable},
            {"autogen",       "2", 1, ttrek_SpecToObj_Autogen},
            {"configure",     "2", 1, ttrek_SpecToObj_Configure},
            {"cmake_config",  "2", 1, ttrek_SpecToObj_CmakeConfig},
            {"make",          "3", 1, ttrek_SpecToObj_Make},
            {"cmake_make",    "3", 1, ttrek_SpecToObj_CmakeMake},
            {"make_install",  "4", 1, ttrek_SpecToObj_MakeInstall},
            {"cmake_install", "4", 1, ttrek_SpecToObj_CmakeInstall},
            {NULL,           NULL, 0, NULL}
    };

    Tcl_Obj *resultList = Tcl_NewListObj(0, NULL);

    if (!cJSON_IsArray(spec)) {
        SetResult("build instructions must be an array");
        Tcl_BounceRefCount(resultList);
        return NULL;
    }
    int named_sources = 0;
    if (!is_local_build && ttrek_ValidateSources(interp, spec, global_use_flags_ht_ptr, &named_sources) != TCL_OK) {
        goto error;
    }
    Tcl_ListObjAppendElement(interp, resultList, Tcl_NewStringObj(named_sources
        ? "_TTREK_UNPACK_ROOT=\"$SOURCE_DIR\"; readonly _TTREK_UNPACK_ROOT"
        : "_TTREK_UNPACK_ROOT=''", -1));
    const cJSON *cmd;
    cJSON_ArrayForEach(cmd, spec) {


        int is_continue;
        if (ttrek_IsUseFlagEnabled(interp, global_use_flags_ht_ptr, cmd, &is_continue) != TCL_OK) {
            goto error;
        }
        if (!is_continue) {
            continue;
        }

        const cJSON *cmdTypeJson = cJSON_GetObjectItem(cmd, "cmd");
        if (!cJSON_IsString(cmdTypeJson)) {
            SetResult("unable to find \"cmd\" object in json spec");
            goto error;
        }

        Tcl_Obj *cmdTypeObj = Tcl_NewStringObj(cJSON_GetStringValue(cmdTypeJson), -1);
        Tcl_IncrRefCount(cmdTypeObj);

        int cmdType;
        int res = Tcl_GetIndexFromObjStruct(interp, cmdTypeObj, commands,
                                            sizeof(commands[0]), "command", 0, &cmdType);
        Tcl_DecrRefCount(cmdTypeObj);

        if (res != TCL_OK) {
            goto error;
        }

        if (is_local_build && !commands[cmdType].enable_in_local_build) {
            DBG2(fprintf(stderr, "skip in local build: [%s]", commands[cmdType].cmd));
            continue;
        }

        if (commands[cmdType].stage != NULL) {
            Tcl_Obj *stageCmd = Tcl_NewStringObj("stage ", -1);
            Tcl_AppendToObj(stageCmd, commands[cmdType].stage, -1);
            Tcl_ListObjAppendElement(interp, resultList, stageCmd);
        }

        if (commands[cmdType].handler(interp, state_ptr, cmd, global_use_flags_ht_ptr, resultList) != TCL_OK) {
            goto error;
        }

    }

    return resultList;

    error:
    Tcl_BounceRefCount(resultList);
    return NULL;
}

Tcl_Obj *ttrek_generatePackageCounter(Tcl_Interp *interp, int package_num_current, int package_num_total) {

    Tcl_Obj *pkg_obj_current, *pkg_obj_total;

    if (package_num_current == -1) {
        pkg_obj_current = Tcl_ObjPrintf(pkg_counter_template, 1);
    } else {
        pkg_obj_current = Tcl_NewIntObj(package_num_current);
    }

    if (package_num_total == -1) {
        pkg_obj_total = Tcl_ObjPrintf(pkg_counter_template, 2);
    } else {
        pkg_obj_total = Tcl_NewIntObj(package_num_total);
    }

    return ttrek_AppendFormatToObj(interp, NULL, pkg_counter_script, 2,
                                   odq(pkg_obj_current), odq(pkg_obj_total));

}

Tcl_Obj *ttrek_generateScriptHeader(const char *shell) {
    Tcl_Obj *header = Tcl_NewStringObj(strcmp(shell, "bash") == 0
        ? "#!/usr/bin/env bash\nset -eo pipefail\n\n"
        : "#!/bin/sh\nset -e\n\n", -1);
    Tcl_AppendToObj(header, script_header, -1);
    Tcl_AppendToObj(header, "\n", 1);
    Tcl_AppendToObj(header, install_script_common_static, -1);
    return header;
}

Tcl_Obj *ttrek_generateInstallScript(Tcl_Interp *interp, const char *package_name,
                                     const char *package_version, const char *source_dir,
                                     cJSON *spec, Tcl_HashTable *global_use_flags_ht_ptr,
                                     ttrek_state_t *state_ptr) {

    Tcl_Obj *install_specific = ttrek_SpecToObj(interp, state_ptr, spec, global_use_flags_ht_ptr,
                                                state_ptr->is_local_build);
    if (install_specific == NULL) {
        return NULL;
    }

    Tcl_Obj *install_common = Tcl_NewObj();

    if (source_dir == NULL) {
        source_dir = "";
    }

    if (state_ptr->mode == MODE_SCRIPT) {

        ttrek_AppendFormatToObj(interp, install_common, script_package, 3,
                                sq(package_name), sq(package_version), state_ptr->is_local_build ? dq("$PROJECT_HOME") : sq(source_dir));

    } else {

        ttrek_AppendFormatToObj(interp, install_common, install_script_common_dynamic, 6,
                                sq(package_name), sq(package_version), osq(state_ptr->project_build_dir_ptr),
                                osq(state_ptr->project_install_dir_ptr), sq(source_dir),
                                osq(state_ptr->project_home_dir_ptr));

        Tcl_AppendToObj(install_common, install_script_common_static, -1);

        Tcl_Obj *package_counter = ttrek_generatePackageCounter(interp, -1, -1);
        Tcl_AppendObjToObj(install_common, package_counter);
        Tcl_BounceRefCount(package_counter);

    }

    Tcl_Obj *install_full = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, install_full, install_common);
    Tcl_ListObjAppendList(interp, install_full, install_specific);
    Tcl_ListObjAppendElement(interp, install_full, Tcl_NewStringObj("ok", -1));

    Tcl_Obj *rc = Tcl_NewObj();

    Tcl_Size listLen;
    Tcl_Obj **elemPtrs;
    Tcl_ListObjGetElements(interp, install_full, &listLen, &elemPtrs);

    // Join everything by new-line
    for (Tcl_Size i = 0; i < listLen; i++) {
        Tcl_AppendObjToObj(rc, elemPtrs[i]);
        Tcl_AppendToObj(rc, "\n", 1);
    }

    Tcl_BounceRefCount(install_full);

    return rc;

}
