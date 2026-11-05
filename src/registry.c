/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <curl/curl.h>
#include "common.h"
#include "registry.h"
#include "ttrek_telemetry.h"

static size_t write_memory_cb(const void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    Tcl_DString *dsPtr = (Tcl_DString *)userp;

    if (dsPtr != NULL) {
        Tcl_DStringAppend(dsPtr, contents, realsize);
    }

    return realsize;
}

static void registry_error(Tcl_DString *result, const char *message) {
    if (result) {
        Tcl_DStringSetLength(result, 0);
        Tcl_DStringAppend(result, message, -1);
    }
}

int ttrek_RegistryGet(const char *url, Tcl_DString *dsPtr, cJSON *postData) {
    if (ttrek_RegistryIsLocal()) {
        // Local registries have no telemetry endpoint.
        return postData ? TCL_OK : ttrek_RegistryGetLocal(url, dsPtr);
    }
    int rc = TCL_OK;

    Tcl_Obj *machineId = ttrek_TelemetryGetMachineId();
    if (machineId != NULL) {
        ttrek_TelemetryRegisterEnvironment();
    }

    DBG2(fprintf(stderr, "enter url: %s", url));
    struct curl_slist *chunk = NULL;
    CURL *curl_handle = curl_easy_init();
    if (!curl_handle) {
        registry_error(dsPtr, "Could not initialize the registry connection");
        return TCL_ERROR;
    }
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_cb);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, dsPtr);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ttrek/1.0");
    // curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);

    if (machineId != NULL) {
        Tcl_Obj *machineIdHdr = Tcl_NewStringObj("TTrek-Environment-Id: ", -1);
        Tcl_IncrRefCount(machineIdHdr);
        Tcl_AppendObjToObj(machineIdHdr, machineId);
        chunk = curl_slist_append(chunk, Tcl_GetString(machineIdHdr));
        Tcl_DecrRefCount(machineIdHdr);
    }

    char *postDataStr = NULL;
    if (postData != NULL) {
        DBG2(fprintf(stderr, "prepare POST request"));
        postDataStr = cJSON_PrintUnformatted(postData);
        if (postDataStr == NULL) {
            registry_error(dsPtr, "Could not serialize the registry request");
            goto error;
        }
        DBG2(fprintf(stderr, "POST data [%s]", postDataStr));
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, postDataStr);
        chunk = curl_slist_append(chunk, "Content-Type: application/json");
    } else {
        DBG2(fprintf(stderr, "prepare GET request"));
    }

    // set our custom set of headers
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, chunk);

    CURLcode ret = curl_easy_perform(curl_handle);
    if (ret == CURLE_OK) {
//        fprintf(stderr, "%lu bytes retrieved\n", Tcl_DStringLength(dsPtr));
    } else {
        registry_error(dsPtr, "Could not read registry: ");
        if (dsPtr) { Tcl_DStringAppend(dsPtr, curl_easy_strerror(ret), -1); }
        goto error;
    }

    DBG2(fprintf(stderr, "ok"));
    goto done;

error:
    rc = TCL_ERROR;

done:
    curl_easy_cleanup(curl_handle);
    // free the custom headers
    curl_slist_free_all(chunk);
    if (postDataStr != NULL) {
        Tcl_Free(postDataStr);
    }
    return rc;
}
