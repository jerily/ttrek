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

static const char bootstrap_script_common_dynamic[] = {
#include "bootstrap_common_dynamic.sh.h"

        0x00
};

static const char install_script_common_static[] = {
#include "install_common_static.sh.h"

        0x00
};

static const char bootstrap_script[] = {
#include "bootstrap.sh.h"

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

        DBG2(printf("check flag \"%s\": %s", flag_str, (*result ? "yes" : "no")));
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
        DBG2(printf("return false (string is empty)"));
        if (interp != NULL) {
            SetResult("shell variable name cannot be an empty string");
        }
        return 0;
    }

    // Check if the first character is a letter or an underscore
    if (!isalpha(str[0]) && str[0] != '_') {
        DBG2(printf("return false (first character is invalid)"));
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("the first character in"
                                                   " the variable name \"%s\" is invalid", str));
        }
        return 0;
    }

    // Check the rest of the string
    for (Tcl_Size i = 1; i < len; i++) {
        if (!isalnum(str[i]) && str[i] != '_') {
            DBG2(printf("return false (character at index %" TCL_SIZE_MODIFIER
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
        DBG2(printf("new cmd: [%s]; log file: [%s]", Tcl_GetString(cmd), Tcl_GetString(log_file)));
        Tcl_AppendToObj(cmd, " >", 2);
        Tcl_AppendObjToObj(cmd, log_file);
        Tcl_AppendToObj(cmd, " 2>&1", 5);
    } else {
        DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
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

DEFINE_COMMAND(EnvVariable) {

    UNUSED(use_flags_ht_ptr);

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

DEFINE_COMMAND(Download) {

    UNUSED(use_flags_ht_ptr);

    Tcl_Obj *cmd;

    Tcl_Obj *url = ttrek_cJSONStringToObject(opts, "url");
    if (url == NULL) {
        SetResult("error while parsing \"download\" cmd: no url or url is not a string");
        return TCL_ERROR;
    }

    if (state_ptr->mode == MODE_BOOTSTRAP) {
        cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd curl -sL -o %s %s",
                                      2, dq("$DOWNLOAD_DIR/$ARCHIVE_FILE"), osq(url));
    } else {
        cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd %s download %s %s",
                                      3, osq(Tcl_NewStringObj(Tcl_GetNameOfExecutable(), -1)), osq(url),
                                      dq("$DOWNLOAD_DIR/$ARCHIVE_FILE"));
    }

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/download.log"));

    return TCL_OK;

}

DEFINE_COMMAND(Patch) {

    UNUSED(use_flags_ht_ptr);

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

DEFINE_COMMAND(Unpack) {

    UNUSED(use_flags_ht_ptr);
    UNUSED(opts);

    Tcl_Obj *cmd;

    if (state_ptr->mode == MODE_BOOTSTRAP) {
        cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd unpack %s %s", 2, dq("$DOWNLOAD_DIR/$ARCHIVE_FILE"),
                                      dq("$SOURCE_DIR"));
    } else {
        cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd %s unpack %s %s", 3,
                                      osq(Tcl_NewStringObj(Tcl_GetNameOfExecutable(), -1)),
                                      dq("$DOWNLOAD_DIR/$ARCHIVE_FILE"), dq("$SOURCE_DIR"));
    }

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/unpack.log"));

    return TCL_OK;

}

DEFINE_COMMAND(Cd) {

    UNUSED(use_flags_ht_ptr);

    Tcl_Obj *cmd;

    Tcl_Obj *dirname = ttrek_cJSONStringToObject(opts, "dirname");

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd cd %s", 1,
                                  (dirname == NULL ? dq("$BUILD_DIR") : odq(dirname)));

    APPEND_CMD(cmd, NULL);

    return TCL_OK;

}

DEFINE_COMMAND(Autogen) {

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

    Tcl_Obj *cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONStringToObject(opts, "ld_library_path");
    if (ld_library_path != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "LD_LIBRARY_PATH=%s ", 1,
                                odq(ld_library_path));
    }

    ttrek_AppendFormatToObj(interp, cmd, "cmd cmake %s", 1, dq("$SOURCE_DIR"));

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

    Tcl_Obj *cmd;

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cmd cmake --install %s", 1,
                                  dq("$BUILD_DIR"));

    Tcl_Obj *config = ttrek_cJSONStringToObject(opts, "config");
    if (config != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, " --config=%s", 1, osq(config));
    }

    APPEND_CMD(cmd, dq("$BUILD_LOG_DIR/install.log"));

    return TCL_OK;

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
        if (cmdTypeJson == NULL) {
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
            DBG2(printf("skip in local build: [%s]", commands[cmdType].cmd));
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

Tcl_Obj *ttrek_generateBootstrapScript(Tcl_Interp *interp, ttrek_state_t *state_ptr) {

    UNUSED(interp);
    UNUSED(state_ptr);

    Tcl_Obj *bootstrap = Tcl_NewObj();

    // ttrek_AppendFormatToObj(interp, bootstrap, bootstrap_script, 3,
    //     osq(state_ptr->project_build_dir_ptr), osq(state_ptr->project_install_dir_ptr),
    //     osq(state_ptr->project_home_dir_ptr));

    Tcl_AppendToObj(bootstrap, bootstrap_script, -1);

    Tcl_AppendToObj(bootstrap, install_script_common_static, -1);

    return bootstrap;

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

    if (state_ptr->mode == MODE_BOOTSTRAP) {

        ttrek_AppendFormatToObj(interp, install_common, bootstrap_script_common_dynamic, 3,
                                sq(package_name), sq(package_version), sq(source_dir));

    } else {

        ttrek_AppendFormatToObj(interp, install_common, install_script_common_dynamic, 5,
                                sq(package_name), sq(package_version), osq(state_ptr->project_build_dir_ptr),
                                osq(state_ptr->project_install_dir_ptr), sq(source_dir));

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
