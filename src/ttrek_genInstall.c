/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "common.h"
#include <string.h>
#include <stdarg.h>

static const char install_script_common_dynamic[] = {
#include "install_common_dynamic.sh.h"
    0x00
};

static const char install_script_common_static[] = {
#include "install_common_static.sh.h"
    0x00
};

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
            (str[i] == '$' && i < (len - 1) && str[i+1] == '('))
        {
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
    int objc, ...)
{
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

static void ttrek_SpecToObj_AppendCommand(Tcl_Interp *interp, Tcl_Obj *resultList, Tcl_Obj *cmd) {
    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_AppendToObj(cmd, " || fail", -1);
    Tcl_ListObjAppendElement(interp, resultList, cmd);
}

#define sq(x) ttrek_StringToSingleQuotedObj(x, -1)
#define dq(x) ttrek_StringToDoubleQuotedObj(x, -1)
#define osq(x) ttrek_ObjectToSingleQuotedObj(x)
#define odq(x) ttrek_ObjectToDoubleQuotedObj(x)

#define APPEND_CMD(x) ttrek_SpecToObj_AppendCommand(interp, resultList, (x));

static int ttrek_SpecToObj_Download(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    Tcl_Obj *url = ttrek_cJSONStringToObject(opts, "url");
    if (url == NULL) {
        SetResult("error while parsing \"download\" cmd: no url or url is not a string");
        return TCL_ERROR;
    }

    cmd = ttrek_AppendFormatToObj(interp, NULL, "%s download %s %s",
        3, osq(Tcl_NewStringObj(Tcl_GetNameOfExecutable(), -1)), osq(url),
        dq("$DOWNLOAD_DIR/$ARCHIVE_FILE"));

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/download.log"));

    APPEND_CMD(cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_Patch(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

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

    APPEND_CMD(cmd);

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cat %s/patch-%s-%s-%s | patch", 4,
        dq("$PATCH_DIR"), dq("$PACKAGE"), dq("$VERSION"), osq(filename));

    Tcl_Obj *p_num = ttrek_cJSONStringToObject(opts, "p_num");
    if (p_num != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, " -p%s", 1, osq(p_num));
    }

    ttrek_AppendFormatToObj(interp, cmd, " >%s/patch-%s.log 2>&1", 2,
        dq("$BUILD_LOG_DIR"), osq(filename));

    APPEND_CMD(cmd);

    Tcl_DecrRefCount(filename);

    return TCL_OK;

}

static int ttrek_SpecToObj_Git(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    Tcl_Obj *url = ttrek_cJSONStringToObject(opts, "url");
    if (url == NULL) {
        SetResult("error while parsing \"git\" cmd: no url or url is not a string");
        return TCL_ERROR;
    }

    cmd = ttrek_AppendFormatToObj(interp, NULL, "git -C %s clone %s --depth 1"
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

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/download.log"));

    APPEND_CMD(cmd);

    cmd = ttrek_AppendFormatToObj(interp, NULL, "find %s -name '.git' -print0 |"
        " xargs -0 rm -rf", 1, dq("$SOURCE_DIR"));

    APPEND_CMD(cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_Unpack(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    cmd = ttrek_AppendFormatToObj(interp, NULL, "%s unpack %s %s", 3,
        osq(Tcl_NewStringObj(Tcl_GetNameOfExecutable(), -1)),
        dq("$DOWNLOAD_DIR/$ARCHIVE_FILE"), dq("$SOURCE_DIR"));

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/unpack.log"));

    APPEND_CMD(cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_Cd(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    Tcl_Obj *dirname = ttrek_cJSONStringToObject(opts, "dirname");

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cd %s", 1,
        (dirname == NULL ? dq("$BUILD_DIR") : odq(dirname)));

    APPEND_CMD(cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_Autogen(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cd %s", 1, dq("$SOURCE_DIR"));

    APPEND_CMD(cmd);

    cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONStringToObject(opts, "ld_library_path");
    if (ld_library_path != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "LD_LIBRARY_PATH=%s ", 1,
            odq(ld_library_path));
    }

    Tcl_Obj *path = ttrek_cJSONStringToObject(opts, "path");
    if (path == NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "%s", 1, dq("./autogen.sh"));
    } else {
        // TODO: make sure the path is under $SOURCE_DIR
        ttrek_AppendFormatToObj(interp, cmd, "%s", 1, odq(path));
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

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/autogen.log"));

    APPEND_CMD(cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_Configure(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONStringToObject(opts, "ld_library_path");
    if (ld_library_path != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "LD_LIBRARY_PATH=%s ", 1,
            odq(ld_library_path));
    }

    Tcl_Obj *path = ttrek_cJSONStringToObject(opts, "path");
    if (path == NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "%s", 1, dq("$SOURCE_DIR/configure"));
    } else {
        // TODO: make sure the path is under $SOURCE_DIR
        ttrek_AppendFormatToObj(interp, cmd, "%s", 1, odq(path));
    }

    Tcl_Obj *option_prefix = ttrek_cJSONStringToObject(opts, "option_prefix");
    if (option_prefix == NULL) {
        option_prefix = Tcl_NewStringObj("--", 2);
    }
    Tcl_IncrRefCount(option_prefix);

    ttrek_AppendFormatToObj(interp, cmd, " %sprefix=%s", 2, osq(option_prefix),
        dq("$INSTALL_DIR"));

    const cJSON *options = cJSON_GetObjectItem(opts, "options");
    if (options == NULL) {
        goto skip_options;
    }

    const cJSON *option;
    cJSON_ArrayForEach(option, options) {

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

skip_options:

    Tcl_DecrRefCount(option_prefix);

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/configure.log"));

    APPEND_CMD(cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_CmakeConfig(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONStringToObject(opts, "ld_library_path");
    if (ld_library_path != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "LD_LIBRARY_PATH=%s ", 1,
            odq(ld_library_path));
    }

    ttrek_AppendFormatToObj(interp, cmd, "cmake %s", 1, dq("$SOURCE_DIR"));

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

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/configure.log"));

    APPEND_CMD(cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_Make(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd = Tcl_NewStringObj("make", -1);

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

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/build.log"));

    APPEND_CMD(cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_CmakeMake(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cmake --build %s", 1,
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

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/build.log"));

    APPEND_CMD(cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_MakeInstall(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONStringToObject(opts, "ld_library_path");
    if (ld_library_path != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "LD_LIBRARY_PATH=%s ", 1,
            odq(ld_library_path));
    }

    Tcl_AppendToObj(cmd, "make install", -1);

    const cJSON *options = cJSON_GetObjectItem(opts, "options");
    if (options == NULL) {
        goto skip_options;
    }

    const cJSON *option;
    cJSON_ArrayForEach(option, options) {

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

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/install.log"));

    APPEND_CMD(cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_CmakeInstall(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cmake --install %s", 1,
            dq("$BUILD_DIR"));

    Tcl_Obj *config = ttrek_cJSONStringToObject(opts, "config");
    if (config != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, " --config=%s", 1, osq(config));
    }

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/install.log"));

    APPEND_CMD(cmd);

    return TCL_OK;

}


Tcl_Obj *ttrek_SpecToObj(Tcl_Interp *interp, cJSON *spec) {

    static const struct {
        const char *cmd;
        const char *stage;
        int (*handler)(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList);
    } commands[] = {
        {"download",       "1", ttrek_SpecToObj_Download},
        {"git",            "1", ttrek_SpecToObj_Git},
        {"unpack",         "1", ttrek_SpecToObj_Unpack},
        {"patch",          "1", ttrek_SpecToObj_Patch},
        {"cd",            NULL, ttrek_SpecToObj_Cd},
        {"autogen",        "2", ttrek_SpecToObj_Autogen},
        {"configure",      "2", ttrek_SpecToObj_Configure},
        {"cmake_config",   "2", ttrek_SpecToObj_CmakeConfig},
        {"make",           "3", ttrek_SpecToObj_Make},
        {"cmake_make",     "3", ttrek_SpecToObj_CmakeMake},
        {"make_install",   "4", ttrek_SpecToObj_MakeInstall},
        {"cmake_install",  "4", ttrek_SpecToObj_CmakeInstall},
        {NULL, 0}
    };

    Tcl_Obj *resultList = Tcl_NewListObj(0, NULL);

    const cJSON *cmd;
    cJSON_ArrayForEach(cmd, spec) {

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

        if (commands[cmdType].stage != NULL) {
            Tcl_Obj *stageCmd = Tcl_NewStringObj("stage ", -1);
            Tcl_AppendToObj(stageCmd, commands[cmdType].stage, -1);
            Tcl_ListObjAppendElement(interp, resultList, stageCmd);
        }

        if (commands[cmdType].handler(interp, cmd, resultList) != TCL_OK) {
            goto error;
        }

    }

    return resultList;

error:
    Tcl_BounceRefCount(resultList);
    return NULL;
}

Tcl_Obj *ttrek_generateInstallScript(Tcl_Interp *interp, const char *package_name,
    const char *package_version, const char *project_build_dir,
    const char *project_install_dir, cJSON *spec)
{

    Tcl_Obj *install_specific = ttrek_SpecToObj(interp, spec);
    if (install_specific == NULL) {
        return NULL;
    }

    Tcl_Obj *install_common = Tcl_NewObj();

    ttrek_AppendFormatToObj(interp, install_common, install_script_common_dynamic, 4,
        sq(package_name), sq(package_version), sq(project_build_dir),
        sq(project_install_dir));

    Tcl_AppendToObj(install_common, install_script_common_static, -1);

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
