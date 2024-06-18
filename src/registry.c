/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <curl/curl.h>
#include "common.h"

struct MemoryStruct {
    char *memory;
    Tcl_Size size;
};

static size_t write_memory_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    Tcl_DString *dsPtr = (Tcl_DString *)userp;

    Tcl_DStringAppend(dsPtr, contents, realsize);

    return realsize;
}

int ttrek_RegistryGet(const char *url, Tcl_DString *dsPtr) {
    CURL *curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_cb);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, dsPtr);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ttrek/1.0");

    CURLcode ret = curl_easy_perform(curl_handle);
    if (ret == CURLE_OK) {
//        fprintf(stderr, "%lu bytes retrieved\n", Tcl_DStringLength(dsPtr));
    } else {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(ret));
//        SetResult("failed to fetch spec file");
        return TCL_ERROR;
    }
    curl_easy_cleanup(curl_handle);
    return TCL_OK;
}