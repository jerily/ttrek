/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "common.h"
#include "registry.h"
#include "ttrek_telemetry.h"
#include <unistd.h>

#if defined(__linux__)
// For clock_gettime()
#include <time.h>
// For gnu_get_libc_version()
#include <gnu/libc-version.h>
// For uname()
#include <sys/utsname.h>
#endif

#include <openssl/sha.h>

static Tcl_Obj *machineIdObj = NULL;
static int isEnvironmentRegistered = 0;
static char *machineIdBaseFile = "machine-id";

// While gathering environment information by function ttrek_TelemetryCollectEnvironmentInfo(),
// we call ttrek_ExecuteCommand(), which uses Tcl_OpenCommandChannel(),
// which requires a live Tcl interpreter.
//
// However, ttrek_TelemetryGetCompilerVersion() is called from ttrek_TelemetryRegisterEnvironment(),
// which is called from ttrek_RegistryGet(). The last function does not have
// a Tcl interpreter as a parameter. Furthermore, ttrek_RegistryGet() cannot
// have a Tcl interpreter as a parameter because it is called from the resolver
// CPP library, where a Tcl interpreter is not available.
//
// Thus, we are in conflict. A live Tcl interpreter is required, but we don't
// have one.
//
// To resolve the conflict, we will save the current live Tcl interpreter into
// the collect_info_interp variable during the machine ID discovery process.
// This variable will be used in the function ttrek_TelemetryCollectEnvironmentInfo().
static Tcl_Interp *collect_info_interp = NULL;

static Tcl_Obj *ttrek_TelemetryReadFile(Tcl_Interp *interp, const char *path) {
   Tcl_Obj *rc = NULL;

   Tcl_Obj *pathObj = Tcl_NewStringObj(path, -1);
   Tcl_IncrRefCount(pathObj);

   if (Tcl_FSAccess(pathObj, R_OK) == -1) {
       goto done;
   }

   rc = Tcl_NewObj();

   if (ttrek_ReadChars(interp, pathObj, &rc) != TCL_OK ||
       !Tcl_GetCharLength(rc))
   {
       Tcl_BounceRefCount(rc);
       rc = NULL;
   } else {
       // Delete a possible new line at the end of the file
        Tcl_Size len;
        char *str = Tcl_GetStringFromObj(rc, &len);
        if (len > 0 && str[len - 1] == '\n') {
            Tcl_SetObjLength(rc, len - 1);
        }
   }

done:
   Tcl_DecrRefCount(pathObj);
   return rc;
}

static int ttrek_TelemetryIsFileExist(const char *path) {
   Tcl_Obj *pathObj = Tcl_NewStringObj(path, -1);
   Tcl_IncrRefCount(pathObj);
   int rc = (Tcl_FSAccess(pathObj, F_OK) == -1 ? 0 : 1);
   Tcl_DecrRefCount(pathObj);
   return rc;
}

static Tcl_Obj *ttrek_TelemetryGetSHA256(Tcl_Obj *data) {
    Tcl_Size size;
    unsigned char *str = Tcl_GetByteArrayFromObj(data, &size);
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

static int ttrek_TelemetryIsDockerEnvironment() {
    return ttrek_TelemetryIsFileExist("/.dockerenv");
}

static Tcl_Obj *ttrek_TelemetryGenerateMachineId(Tcl_Interp *interp) {
    Tcl_Obj *machineIdRaw = NULL;
    DBG2(printf("enter ..."));

#if defined(_WIN32)
    DBG2(printf("trying to detect machine id for windows ..."));
    // Here should be the code that retrieves the host ID from the registry
    // key: HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Cryptograpy\MachineGuid
    goto done;

#elif defined(__APPLE__)
    DBG2(printf("trying to detect machine id for macos ..."));
    {
        Tcl_Size argc = 5;
        const char *argv[6] = {
            "/usr/sbin/ioreg", "-rd1", "-c", "IOPlatformExpertDevice", "2>@1", NULL
        };
        machineIdRaw = Tcl_NewObj();
        DBG2(printf("exec /usr/sbin/ioreg ..."));
        if (ttrek_ExecuteCommand(interp, argc, argv, machineIdRaw) != TCL_OK
            || !Tcl_GetCharLength(machineIdRaw))
        {
            Tcl_BounceRefCount(machineIdRaw);
            machineIdRaw = NULL;
            goto done;
        }
    }

#elif defined(__linux__)
    DBG2(printf("trying to detect machine id for linux ..."));

    if (ttrek_TelemetryIsDockerEnvironment()) {
        DBG2(printf("docker environment detected, skip checking */machine-id"
            " files"));
        goto docker_detected;
    }

    DBG2(printf("read file: /var/lib/dbus/machine-id"));
    machineIdRaw = ttrek_TelemetryReadFile(interp, "/var/lib/dbus/machine-id");
    if (machineIdRaw != NULL) {
        goto done;
    }

    DBG2(printf("read file: /etc/machine-id"));
    machineIdRaw = ttrek_TelemetryReadFile(interp, "/etc/machine-id");
    if (machineIdRaw != NULL) {
        goto done;
    }

docker_detected:

    // For the docker environment and if no */machine-id files are available,
    // define the machine-id based on boot time and linux kernel version.
    DBG2(printf("generate machine id based on boot time and kernel"
        " version..."));

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // Wrap timestamp in raw machine id data.
    machineIdRaw = Tcl_NewByteArrayObj((unsigned char *)&ts, sizeof(ts));

    struct utsname unamebuf;
    if (uname(&unamebuf) != 0) {
        DBG2(printf("WARNING: uname() failed"));
        goto done;
    }
    Tcl_AppendStringsToObj(machineIdRaw, unamebuf.release, unamebuf.version,
        unamebuf.machine, (char *)NULL);

#elif defined(__unix__)
    DBG2(printf("trying to detect machine id for BSD ..."));

    DBG2(printf("read file: /etc/hostid"));
    machineIdRaw = ttrek_TelemetryReadFile(interp, "/etc/hostid");
    if (machineIdRaw != NULL) {
        goto done;
    }

    {
        Tcl_Size argc = 4;
        const char *argv[5] = {
            "/bin/kenv", "-q", "smbios.system.uuid", "2>@1", NULL
        };
        machineIdRaw = Tcl_NewObj();
        DBG2(printf("exec /bin/kenv ..."));
        if (ttrek_ExecuteCommand(interp, argc, argv, machineIdRaw) != TCL_OK
            || !Tcl_GetCharLength(machineIdRaw))
        {
            Tcl_BounceRefCount(machineIdRaw);
            machineIdRaw = NULL;
            goto done;
        }
    }

#else
    DBG2(printf("unknown OS"));
    goto done;
#endif

done:
    if (machineIdRaw == NULL) {
        DBG2(printf("failed; return NULL"));
        return NULL;
    }
    Tcl_IncrRefCount(machineIdRaw);
    // The real machine ID may be security sensitive information that
    // should not be shared outside the machine. To avoid disclosure of
    // this information, we will use its hash.
    Tcl_Obj *rc = ttrek_TelemetryGetSHA256(machineIdRaw);
    Tcl_DecrRefCount(machineIdRaw);
    DBG2(printf("return: ok"));
    return rc;
}

Tcl_Obj *ttrek_TelemetryGetMachineIdFile(Tcl_Interp *interp) {
    Tcl_Obj *machineIdFile;
    if (ttrek_ResolvePathUserHome(interp, Tcl_NewStringObj(machineIdBaseFile, -1),
        &machineIdFile) != TCL_OK)
    {
        DBG2(printf("ERROR: failed to get the path to the machine-id file"));
        return NULL;
    }
    return machineIdFile;
}

void ttrek_TelemetrySaveMachineId(Tcl_Interp *interp) {
    if (machineIdObj == NULL) {
        return;
    }
    Tcl_Obj *machineIdFile = ttrek_TelemetryGetMachineIdFile(interp);
    if (machineIdFile == NULL) {
        return;
    }
    if (ttrek_CheckFileExists(machineIdFile) == TCL_OK) {
        DBG2(printf("the file already exists"));
        goto done;
    }
    if (ttrek_WriteChars(interp, machineIdFile, machineIdObj, 0644) == TCL_OK) {
        DBG2(printf("ok"));
    } else {
        DBG2(printf("ERROR: failed to write the machineId to the file"));
    }
done:
    Tcl_DecrRefCount(machineIdFile);
}

void ttrek_TelemetryLoadMachineId(Tcl_Interp *interp) {
    collect_info_interp = interp;
    Tcl_Obj *machineIdFile = ttrek_TelemetryGetMachineIdFile(interp);
    machineIdObj = ttrek_TelemetryReadFile(interp, Tcl_GetString(machineIdFile));
    Tcl_DecrRefCount(machineIdFile);
    if (machineIdObj != NULL) {
        DBG2(printf("successfully read the machine id from the home directory"));
        return;
    }
    DBG2(printf("failed to read the machine id from the home directory"));
    machineIdObj = ttrek_TelemetryGenerateMachineId(interp);
}

void ttrek_TelemetryFree(void) {
    if (machineIdObj != NULL) {
        DBG2(printf("free ..."));
        Tcl_DecrRefCount(machineIdObj);
        machineIdObj = NULL;
    }
}

static cJSON *ttrek_TelemetryGetCompilerVersion(Tcl_Interp *interp,
    const char *cc)
{
    Tcl_Obj *result = Tcl_NewObj();
    Tcl_IncrRefCount(result);

    Tcl_Size argc = 3;
    const char *argv[4];
    argv[0] = cc;
    argv[2] = "2>@1";
    argv[3] = NULL;

    // In this function, we will try to get a short version of the compiler.
    // We don't know what type of compiler we are testing or what command
    // line option we can use to get the short version. We will try
    // the following command line options until the compiler outputs
    // something and exits with zero termination code:
    //     -dumpfullversion, -dumpversion, --version

    const char *compiler_opts[3] = {
        "-dumpfullversion", "-dumpversion", "--version"
    };

    for (int i = 0; i < 3; i++) {
        argv[1] = compiler_opts[i];
        if (ttrek_ExecuteCommand(interp, argc, argv, result) == TCL_OK
            && Tcl_GetCharLength(result))
        {
            break;
        }
    }

    cJSON *rc = cJSON_CreateString(Tcl_GetString(result));
    Tcl_DecrRefCount(result);

    return rc;
}

static cJSON *ttrek_TelemetryCollectEnvironmentInfo() {
    cJSON *val;
    cJSON *root = cJSON_CreateObject();
#if defined(_WIN32)
    val = cJSON_CreateString("windows");
#elif defined(__APPLE__)
    val = cJSON_CreateString("macos");
#elif defined(__linux__)
    val = cJSON_CreateString("linux");
#elif defined(__unix__)
    val = cJSON_CreateString("bsd");
#else
    val = cJSON_CreateString("unknown");
#endif
    cJSON_AddItemToObject(root, "os", val);

    cJSON_AddItemToObject(root, "ttrek_version",
        cJSON_CreateString(XSTR(PROJECT_VERSION)));

#if defined(__linux__)
    val = (ttrek_TelemetryIsDockerEnvironment() ? cJSON_CreateTrue() :
        cJSON_CreateFalse());
    cJSON_AddItemToObject(root, "is_docker_env", val);

    struct utsname unamebuf;
    if (uname(&unamebuf) == 0) {
        cJSON *uname = cJSON_CreateObject();
        cJSON_AddItemToObject(uname, "release",
            cJSON_CreateString(unamebuf.release));
        cJSON_AddItemToObject(uname, "version",
            cJSON_CreateString(unamebuf.version));
        cJSON_AddItemToObject(uname, "machine",
            cJSON_CreateString(unamebuf.machine));
        cJSON_AddItemToObject(root, "uname", uname);
    }
#endif

    cJSON *versions = cJSON_CreateObject();
    cJSON *environment = cJSON_CreateObject();

    const char *env_var;

    env_var = ttrek_EnvVarGet(collect_info_interp, "CC");
    if (env_var != NULL) {
        cJSON_AddItemToObject(environment, "CC", cJSON_CreateString(env_var));
    }
    cJSON_AddItemToObject(versions, "cc",
        ttrek_TelemetryGetCompilerVersion(collect_info_interp,
        (env_var == NULL ? "gcc" : env_var)));

    env_var = ttrek_EnvVarGet(collect_info_interp, "CXX");
    if (env_var != NULL) {
        cJSON_AddItemToObject(environment, "CXX", cJSON_CreateString(env_var));
    }
    cJSON_AddItemToObject(versions, "cpp",
        ttrek_TelemetryGetCompilerVersion(collect_info_interp,
        (env_var == NULL ? "g++" : env_var)));

#if defined(__linux__)
    cJSON_AddItemToObject(versions, "glibc",
        cJSON_CreateString(gnu_get_libc_version()));
#endif

    cJSON_AddItemToObject(root, "environment", environment);
    cJSON_AddItemToObject(root, "versions", versions);

    return root;
}

void ttrek_TelemetryRegisterEnvironment() {
    if (isEnvironmentRegistered) {
        return;
    }
    isEnvironmentRegistered = 1;
    if (machineIdObj == NULL) {
        DBG2(printf("machine id is not defined"));
        return;
    }
    DBG2(printf("register the environment"));
    cJSON *env_info = ttrek_TelemetryCollectEnvironmentInfo();
    char register_env_url[256];
    snprintf(register_env_url, sizeof(register_env_url), "%s/%s",
        TELEMETRY_URL, Tcl_GetString(machineIdObj));
    ttrek_RegistryGet(register_env_url, NULL, env_info);
    cJSON_Delete(env_info);
    return;
}

Tcl_Obj *ttrek_TelemetryGetMachineId() {
    return machineIdObj;
}
