/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <curl/curl.h>
#include "common.h"
#include "ttrek_telemetry.h"

struct MemoryStruct {
    char *memory;
    Tcl_Size size;
};

static size_t write_memory_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    Tcl_DString *dsPtr = (Tcl_DString *)userp;

    if (dsPtr != NULL) {
        Tcl_DStringAppend(dsPtr, contents, realsize);
    }

    return realsize;
}

int ttrek_RegistryGet(const char *url, Tcl_DString *dsPtr, cJSON *postData) {
    int rc = TCL_OK;

    Tcl_Obj *machineId = ttrek_TelemetryGetMachineId();
    if (machineId != NULL) {
        ttrek_TelemetryRegisterEnvironment();
    }

    DBG2(printf("enter url: %s", url));
    struct curl_slist *chunk = NULL;
    CURL *curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
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

    char *postDataEncoded = NULL;
    if (postData != NULL) {
        DBG2(printf("prepare POST request"));
        char *postDataStr = cJSON_PrintUnformatted(postData);
        if (postDataStr == NULL) {
            goto error;
        }
        DBG2(printf("POST raw data [%s]", postDataStr));
        postDataEncoded = curl_easy_escape(curl_handle, postDataStr, 0);
        Tcl_Free(postDataStr);
        curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, postDataEncoded);
        DBG2(printf("POST data [%s]", postDataEncoded));
        chunk = curl_slist_append(chunk, "Content-Type: application/x-www-form-urlencoded");
    } else {
        DBG2(printf("prepare GET request"));
    }

    // set our custom set of headers
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, chunk);

    CURLcode ret = curl_easy_perform(curl_handle);
    if (ret == CURLE_OK) {
//        fprintf(stderr, "%lu bytes retrieved\n", Tcl_DStringLength(dsPtr));
    } else {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(ret));
//        SetResult("failed to fetch spec file");
        goto error;
    }

    goto done;

error:
    rc = TCL_ERROR;

done:
    curl_easy_cleanup(curl_handle);
    // free the custom headers
    curl_slist_free_all(chunk);
    if (postDataEncoded != NULL) {
        curl_free(postDataEncoded);
    }
    return rc;
}