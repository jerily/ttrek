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
// For uname()
#include <sys/utsname.h>
#endif

#include <openssl/sha.h>

static Tcl_Obj *machineIdObj = NULL;
static int isEnvironmentRegistered = 0;

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

Tcl_Obj *ttrek_TelemetryGenerateMachineId(Tcl_Interp *interp) {
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

    if (ttrek_TelemetryIsFileExist("/.dockerenv")) {
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

void ttrek_TelemetrySetMachineId(const char *machineId) {
    DBG2(printf("enter ..."));
    ttrek_TelemetryFree();
    if (machineId != NULL) {
        machineIdObj = Tcl_NewStringObj(machineId, -1);
        Tcl_IncrRefCount(machineIdObj);
        DBG2(printf("machineId is %s", Tcl_GetString(machineIdObj)));
    } else {
        DBG2(printf("machineId is NULL"));
    }
}

Tcl_Obj *ttrek_TelemetryGetMachineId() {
    return machineIdObj;
}

void ttrek_TelemetryFree(void) {
    if (machineIdObj != NULL) {
        DBG2(printf("free ..."));
        Tcl_DecrRefCount(machineIdObj);
        machineIdObj = NULL;
    }
}

static cJSON *ttrek_TelemetryCollectEnvironmentInfo() {
    cJSON *root = cJSON_CreateObject();
#if defined(_WIN32)
    cJSON *os = cJSON_CreateString("windows");
#elif defined(__APPLE__)
    cJSON *os = cJSON_CreateString("macos");
#elif defined(__linux__)
    cJSON *os = cJSON_CreateString("linux");
#elif defined(__unix__)
    cJSON *os = cJSON_CreateString("bsd");
#else
    cJSON *os = cJSON_CreateString("unknown");
#endif
    cJSON_AddItemToObject(root, "os", os);
    return root;
}

void ttrek_TelemetryRegisterEnvironment() {
    if (machineIdObj == NULL || isEnvironmentRegistered) {
        DBG2(printf("already registered"));
        return;
    }
    isEnvironmentRegistered = 1;
    DBG2(printf("register the environment"));
    cJSON *env_info = ttrek_TelemetryCollectEnvironmentInfo();
    char register_env_url[256];
    snprintf(register_env_url, sizeof(register_env_url), "%s/%s",
        TELEMETRY_URL, Tcl_GetString(machineIdObj));
    ttrek_RegistryGet(register_env_url, NULL, env_info);
    cJSON_Delete(env_info);
    return;
}

