/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "common.h"
#include <string.h>
#include <stdarg.h>

// Below we assume that the printf placeholders in this template are enclosed
// in single quotes in the shell script. Double quotes cannot be used here.
// If any placeholder must be enclosed in double quotes, a function other
// than ttrek_StringToEscapedObj() must be used to properly escape characters
// in the string.

#define L(s) s "\n"
static char *install_script_common =
    L("#!/bin/bash")
    L("")
    L("set -eo pipefail # exit on error")
    L("")
    L("PACKAGE=%s")
    L("VERSION=%s")
    L("ROOT_BUILD_DIR=%s")
    L("INSTALL_DIR=%s")
    L("")
    L("echo \"Installing to $INSTALL_DIR\"")
    L("")
    L("DOWNLOAD_DIR=\"$ROOT_BUILD_DIR/download\"")
    L("ARCHIVE_FILE=\"${PACKAGE}-${VERSION}.archive\"")
    L("SOURCE_DIR=\"$ROOT_BUILD_DIR/source/${PACKAGE}-${VERSION}\"")
    L("BUILD_DIR=\"$ROOT_BUILD_DIR/build/${PACKAGE}-${VERSION}\"")
    L("PATCH_DIR=\"$ROOT_BUILD_DIR/source\"")
    L("BUILD_LOG_DIR=\"$ROOT_BUILD_DIR/logs/${PACKAGE}-${VERSION}\"")
    L("")
    L("mkdir -p \"$DOWNLOAD_DIR\"")
    L("rm -rf \"$SOURCE_DIR\"")
    L("mkdir -p \"$SOURCE_DIR\"")
    L("rm -rf \"$BUILD_DIR\"")
    L("mkdir -p \"$BUILD_DIR\"")
    L("rm -rf \"$BUILD_LOG_DIR\"")
    L("mkdir -p \"$BUILD_LOG_DIR\"")
    L("")
    L("LD_LIBRARY_PATH=\"$INSTALL_DIR/lib\"")
    L("PKG_CONFIG_PATH=\"$INSTALL_DIR/lib/pkgconfig\"")
    L("export LD_LIBRARY_PATH")
    L("export PKG_CONFIG_PATH")
    L("");
#undef L

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

static Tcl_Obj *ttrek_cJSONObjectToObject(const cJSON *json, const char *key) {
    const cJSON *obj = cJSON_GetObjectItem(json, key);
    if (obj == NULL) {
        return NULL;
    }
    return Tcl_NewStringObj(cJSON_GetStringValue(obj), -1);
}

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

#define sq(x) ttrek_StringToSingleQuotedObj(x, -1)
#define dq(x) ttrek_StringToDoubleQuotedObj(x, -1)
#define osq(x) ttrek_ObjectToSingleQuotedObj(x)
#define odq(x) ttrek_ObjectToDoubleQuotedObj(x)

static int ttrek_SpecToObj_Download(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    Tcl_Obj *url = ttrek_cJSONObjectToObject(opts, "url");
    if (url == NULL) {
        SetResult("error while parsing \"download\" cmd: no url");
        return TCL_ERROR;
    }

    cmd = ttrek_AppendFormatToObj(interp, NULL, "LD_LIBRARY_PATH= curl --fail"
        " --silent --show-error -L -o %s --output-dir %s %s", 3,
        dq("$ARCHIVE_FILE"), dq("$DOWNLOAD_DIR"), osq(url));

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_Patch(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    Tcl_Obj *filename = ttrek_cJSONObjectToObject(opts, "filename");
    if (filename == NULL) {
        SetResult("error while parsing \"patch\" cmd: no filename");
        return TCL_ERROR;
    }

    // The filename variable will be used 2 times in this command. We need
    // to increase refcount so that we don't release it after the first use.
    Tcl_IncrRefCount(filename);

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cd %s", 1, dq("$SOURCE_DIR"));

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cat %s/patch-%s-%s-%s | patch", 4,
        dq("$PATCH_DIR"), dq("$PACKAGE"), dq("$VERSION"), osq(filename));

    Tcl_Obj *p_num = ttrek_cJSONObjectToObject(opts, "p_num");
    if (p_num != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, " -p%s", 1, osq(p_num));
    }

    ttrek_AppendFormatToObj(interp, cmd, " >%s/patch-%s.log 2>&1", 2,
        dq("$BUILD_LOG_DIR"), osq(filename));

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    Tcl_DecrRefCount(filename);

    return TCL_OK;

}

static int ttrek_SpecToObj_Git(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    Tcl_Obj *url = ttrek_cJSONObjectToObject(opts, "url");
    if (url == NULL) {
        SetResult("error while parsing \"git\" cmd: no url");
        return TCL_ERROR;
    }

    cmd = ttrek_AppendFormatToObj(interp, NULL, "git -C %s clone %s --depth 1"
        " --single-branch", 2, dq("$SOURCE_DIR"), osq(url));

    Tcl_Obj *branch = ttrek_cJSONObjectToObject(opts, "branch");
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

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    cmd = ttrek_AppendFormatToObj(interp, NULL, "find %s -name '.git' -print0 |"
        " xargs -0 rm -rf", 1, dq("$SOURCE_DIR"));

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_Unpack(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    static const char *const formats[] = {
        "tar.gz", "zip", NULL
    };

    enum formats {
        formatTarGz, formatZip
    };

    int format;

    Tcl_Obj *formatObj = ttrek_cJSONObjectToObject(opts, "format");
    if (formatObj != NULL) {

        int res = Tcl_GetIndexFromObj(interp, formatObj, formats, "unpack format", 0,
            &format);
        Tcl_BounceRefCount(formatObj);

        if (res != TCL_OK) {
            return TCL_ERROR;
        }

    } else {
        format = formatTarGz;
    }

    switch ((enum formats) format) {
    case formatZip:

        cmd = ttrek_AppendFormatToObj(interp, NULL, "unzip %s -d %s", 2,
            dq("$DOWNLOAD_DIR/$ARCHIVE_FILE"), dq("$SOURCE_DIR"));

        DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
        Tcl_ListObjAppendElement(interp, resultList, cmd);

        cmd = ttrek_AppendFormatToObj(interp, NULL, "TEMP=\"$(echo %s/*)\"", 1,
            dq("$SOURCE_DIR"));

        DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
        Tcl_ListObjAppendElement(interp, resultList, cmd);

        cmd = ttrek_AppendFormatToObj(interp, NULL, "mv %s/* %s", 2,
            dq("$TEMP"), dq("$SOURCE_DIR"));

        DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
        Tcl_ListObjAppendElement(interp, resultList, cmd);

        cmd = ttrek_AppendFormatToObj(interp, NULL, "rm -rf %s", 1,
            dq("$TEMP"));

        DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
        Tcl_ListObjAppendElement(interp, resultList, cmd);

        break;

    case formatTarGz:

        cmd = ttrek_AppendFormatToObj(interp, NULL, "tar -xzf %s --strip-components=1"
            " -C %s", 2, dq("$DOWNLOAD_DIR/$ARCHIVE_FILE"), dq("$SOURCE_DIR"));

        DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
        Tcl_ListObjAppendElement(interp, resultList, cmd);

        break;

    }

    return TCL_OK;

}

static int ttrek_SpecToObj_Cd(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    Tcl_Obj *dirname = ttrek_cJSONObjectToObject(opts, "dirname");

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cd %s", 1,
        (dirname == NULL ? dq("$BUILD_DIR") : odq(dirname)));

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_Autogen(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cd %s", 1, dq("$SOURCE_DIR"));

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONObjectToObject(opts, "ld_library_path");
    if (ld_library_path != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "LD_LIBRARY_PATH=%s ", 1,
            odq(ld_library_path));
    }

    Tcl_Obj *path = ttrek_cJSONObjectToObject(opts, "path");
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

    Tcl_Obj *option_prefix = ttrek_cJSONObjectToObject(opts, "option_prefix");
    if (option_prefix == NULL) {
        option_prefix = Tcl_NewStringObj("--", 2);
    }
    Tcl_IncrRefCount(option_prefix);

    const cJSON *option;
    cJSON_ArrayForEach(option, options) {

        Tcl_Obj *name = ttrek_cJSONObjectToObject(option, "name");
        if (name == NULL) {
            continue;
        }

        Tcl_Obj *value = ttrek_cJSONObjectToObject(option, "value");

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

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_Configure(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONObjectToObject(opts, "ld_library_path");
    if (ld_library_path != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "LD_LIBRARY_PATH=%s ", 1,
            odq(ld_library_path));
    }

    Tcl_Obj *path = ttrek_cJSONObjectToObject(opts, "path");
    if (path == NULL) {
        ttrek_AppendFormatToObj(interp, cmd, "%s", 1, dq("$SOURCE_DIR/configure"));
    } else {
        // TODO: make sure the path is under $SOURCE_DIR
        ttrek_AppendFormatToObj(interp, cmd, "%s", 1, odq(path));
    }

    Tcl_Obj *option_prefix = ttrek_cJSONObjectToObject(opts, "option_prefix");
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

        Tcl_Obj *name = ttrek_cJSONObjectToObject(option, "name");
        if (name == NULL) {
            continue;
        }

        Tcl_Obj *value = ttrek_cJSONObjectToObject(option, "value");

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

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_CmakeConfig(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONObjectToObject(opts, "ld_library_path");
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

        Tcl_Obj *name = ttrek_cJSONObjectToObject(option, "name");
        if (name == NULL) {
            continue;
        }

        Tcl_Obj *value = ttrek_cJSONObjectToObject(option, "value");

        if (value == NULL) {
            ttrek_AppendFormatToObj(interp, cmd, " %s", 1, odq(name));
        } else {
            ttrek_AppendFormatToObj(interp, cmd, " -D%s=%s", 2, osq(name), odq(value));
        }

    }

skip_options:

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/configure.log"));

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_Make(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd = Tcl_NewStringObj("make", -1);

    const cJSON *options = cJSON_GetObjectItem(opts, "options");
    if (options == NULL) {
        goto skip_options;
    }

    const cJSON *option;
    cJSON_ArrayForEach(option, options) {

        Tcl_Obj *name = ttrek_cJSONObjectToObject(option, "name");
        if (name == NULL) {
            continue;
        }

        Tcl_Obj *value = ttrek_cJSONObjectToObject(option, "value");

        if (value == NULL) {
            ttrek_AppendFormatToObj(interp, cmd, " %s", 1, odq(name));
        } else {
            ttrek_AppendFormatToObj(interp, cmd, " %s=%s", 2, odq(name), odq(value));
        }

    }

skip_options:

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/build.log"));

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_CmakeMake(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cmake --build %s", 1,
            dq("$BUILD_DIR"));

    Tcl_Obj *config = ttrek_cJSONObjectToObject(opts, "config");
    if (config != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, " --config=%s", 1, osq(config));
    }

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/build.log"));

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_MakeInstall(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd = Tcl_NewObj();

    Tcl_Obj *ld_library_path = ttrek_cJSONObjectToObject(opts, "ld_library_path");
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

        Tcl_Obj *name = ttrek_cJSONObjectToObject(option, "name");
        if (name == NULL) {
            continue;
        }

        Tcl_Obj *value = ttrek_cJSONObjectToObject(option, "value");

        if (value == NULL) {
            ttrek_AppendFormatToObj(interp, cmd, " %s", 1, odq(name));
        } else {
            ttrek_AppendFormatToObj(interp, cmd, " %s=%s", 2, odq(name), odq(value));
        }

    }

skip_options:

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/install.log"));

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    return TCL_OK;

}

static int ttrek_SpecToObj_CmakeInstall(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList) {

    Tcl_Obj *cmd;

    cmd = ttrek_AppendFormatToObj(interp, NULL, "cmake --install %s", 1,
            dq("$BUILD_DIR"));

    Tcl_Obj *config = ttrek_cJSONObjectToObject(opts, "config");
    if (config != NULL) {
        ttrek_AppendFormatToObj(interp, cmd, " --config=%s", 1, osq(config));
    }

    ttrek_AppendFormatToObj(interp, cmd, " >%s 2>&1", 1,
        dq("$BUILD_LOG_DIR/install.log"));

    DBG2(printf("new cmd: [%s]", Tcl_GetString(cmd)));
    Tcl_ListObjAppendElement(interp, resultList, cmd);

    return TCL_OK;

}


Tcl_Obj *ttrek_SpecToObj(Tcl_Interp *interp, cJSON *spec) {

    static const struct {
        const char *cmd;
        int (*handler)(Tcl_Interp *interp, const cJSON *opts, Tcl_Obj *resultList);
    } commands[] = {
        {"download",      ttrek_SpecToObj_Download},
        {"git",           ttrek_SpecToObj_Git},
        {"unpack",        ttrek_SpecToObj_Unpack},
        {"patch",         ttrek_SpecToObj_Patch},
        {"cd",            ttrek_SpecToObj_Cd},
        {"autogen",       ttrek_SpecToObj_Autogen},
        {"configure",     ttrek_SpecToObj_Configure},
        {"cmake_config",  ttrek_SpecToObj_CmakeConfig},
        {"make",          ttrek_SpecToObj_Make},
        {"cmake_make",    ttrek_SpecToObj_CmakeMake},
        {"make_install",  ttrek_SpecToObj_MakeInstall},
        {"cmake_install", ttrek_SpecToObj_CmakeInstall},
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

    Tcl_Obj *objs[4];
    objs[0] = ttrek_StringToSingleQuotedObj(package_name, -1);
    Tcl_IncrRefCount(objs[0]);
    objs[1] = ttrek_StringToSingleQuotedObj(package_version, -1);
    Tcl_IncrRefCount(objs[1]);
    objs[2] = ttrek_StringToSingleQuotedObj(project_build_dir, -1);
    Tcl_IncrRefCount(objs[2]);
    objs[3] = ttrek_StringToSingleQuotedObj(project_install_dir, -1);
    Tcl_IncrRefCount(objs[3]);

    Tcl_Obj *install_common = Tcl_Format(interp, install_script_common, 4, objs);

    Tcl_DecrRefCount(objs[0]);
    Tcl_DecrRefCount(objs[1]);
    Tcl_DecrRefCount(objs[2]);
    Tcl_DecrRefCount(objs[3]);

    if (install_common == NULL) {
        Tcl_BounceRefCount(install_specific)
        return NULL;
    }

    Tcl_Obj *install_full = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, install_full, install_common);
    Tcl_ListObjAppendList(interp, install_full, install_specific);

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
