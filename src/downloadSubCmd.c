/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include <curl/curl.h>

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    return Tcl_Write((Tcl_Channel)userdata, ptr, size * nmemb);
}

int ttrek_DownloadSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    if (objc != 3) {
        SetResult("not enough arguments");
        return TCL_ERROR;
    }

    Tcl_Channel chan = Tcl_FSOpenFileChannel(interp, objv[2], "wb", 0644);
    if (chan == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unable to open output file for writing:"
            " \"%s\" (%s[%d] - %s)", Tcl_GetString(objv[2]), Tcl_ErrnoId(), Tcl_GetErrno(),
            Tcl_ErrnoMsg(Tcl_GetErrno())));
        return TCL_ERROR;
    }

    CURL *curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, Tcl_GetString(objv[1]));
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
        return TCL_ERROR;
    }

    curl_easy_cleanup(curl_handle);

    // Return TCL_OK if all is good
    if (ret == CURLE_OK) {
        return TCL_OK;
    }

    // Produce an error message

    if (ret == CURLE_HTTP_RETURNED_ERROR) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad HTTP status code (%ld)"
            " while downloading \"%s\"", status_code, Tcl_GetString(objv[1])));
    } else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("error while downloading"
            " \"%s\": %s", Tcl_GetString(objv[1]), curl_easy_strerror(ret)));
    }

    return TCL_ERROR;

}
