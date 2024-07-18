/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include <curl/curl.h>

#define TTREK_DOWNLOAD_DEFAULT_RETRY_COUNT 3

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    return Tcl_Write((Tcl_Channel)userdata, ptr, size * nmemb);
}

static int ttrek_DownloadFile(Tcl_Interp *interp, Tcl_Obj *url_ptr, Tcl_Obj *file_ptr) {

    DBG2(printf("URL[%s] -> [%s]", Tcl_GetString(url_ptr), Tcl_GetString(file_ptr)));

    Tcl_Channel chan = Tcl_FSOpenFileChannel(interp, file_ptr, "wb", 0644);
    if (chan == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unable to open output file"
            " for writing: \"%s\" (%s[%d] - %s)", Tcl_GetString(file_ptr),
            Tcl_ErrnoId(), Tcl_GetErrno(), Tcl_ErrnoMsg(Tcl_GetErrno())));
        return TCL_ERROR;
    }

    CURL *curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, Tcl_GetString(url_ptr));
    // Follow redirects
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)chan);

    CURLcode ret = curl_easy_perform(curl_handle);

    // Get a status code. We will use it to show a more detailed message,
    // since the standard curl message is uninformative in this case:
    //     HTTP response code said error
    long status_code = -1;
    curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &status_code);

    if (Tcl_Close(interp, chan) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("error while closing output"
            " channel: %s[%d] - %s", Tcl_ErrnoId(), Tcl_GetErrno(),
            Tcl_ErrnoMsg(Tcl_GetErrno())));
        goto error;
    }

    curl_easy_cleanup(curl_handle);

    // Return TCL_OK if all is good
    if (ret == CURLE_OK) {
        return TCL_OK;
    }

    // Produce an error message

    if (ret == CURLE_HTTP_RETURNED_ERROR) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad HTTP status code (%ld)"
            " while downloading \"%s\"", status_code, Tcl_GetString(url_ptr)));
    } else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("error while downloading"
            " \"%s\": %s", Tcl_GetString(url_ptr), curl_easy_strerror(ret)));
    }

error:
    Tcl_FSDeleteFile(file_ptr);
    return TCL_ERROR;

}

int ttrek_DownloadSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    if (objc != 3) {
        SetResult("not enough arguments");
        return TCL_ERROR;
    }

    Tcl_Obj *url_ptr = objv[1];
    Tcl_Obj *file_ptr = objv[2];

    int retry_current = 0;
    // Maximum number of retries
    int retry_count = TTREK_DOWNLOAD_DEFAULT_RETRY_COUNT;
    int rc = TCL_OK;

    while(1) {

        if (++retry_current != 1) {
            fprintf(stdout, "Attempt %d of %d to download file: %s ...\n", retry_current,
                retry_count, Tcl_GetString(url_ptr));
            fflush(stdout);
        }

        rc = ttrek_DownloadFile(interp, url_ptr, file_ptr);

        // If we successfully downloaded the file, then return TCL_OK. In this
        // case, we don't need to do anything else.
        if (rc == TCL_OK) {
            return TCL_OK;
        }

        // If the number of attempts is exhausted, then exit from the loop
        // to try a fallback URL.
        if (retry_current >= retry_count) {
            break;
        }

        // Show an error message if the download fails and we will make
        // another attempt.
        fprintf(stderr, "WARNING: download failed with: %s\n", Tcl_GetStringResult(interp));
        fflush(stderr);

    };

    // Try using a fallback URL
    Tcl_Obj *url_hash_ptr = ttrek_GetHashSHA256(url_ptr);

    Tcl_Obj *url_fallback_ptr = Tcl_ObjPrintf("%s/%s.archive", DOWNLOAD_URL,
        Tcl_GetString(url_hash_ptr));

    Tcl_BounceRefCount(url_hash_ptr);

    fprintf(stdout, "Attempt to use a fallback URL to download the file ...\n");
    fflush(stdout);

    rc = ttrek_DownloadFile(interp, url_fallback_ptr, file_ptr);

    Tcl_BounceRefCount(url_fallback_ptr);

    // Return what we got from the fallback URL. If there is an error there,
    // we can't do anything else.

    return rc;

}
