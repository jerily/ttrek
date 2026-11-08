#include <curl/curl.h>
#include "curlCA.h"
#include "registry.h"

static size_t receive(void *data, size_t size, size_t count, void *context) {
    Tcl_DStringAppend((Tcl_DString *)context, data, size * count);
    return size * count;
}

static int fail(Tcl_DString *result, const char *message) {
    Tcl_DStringSetLength(result, 0);
    Tcl_DStringAppend(result, message, -1);
    return TCL_ERROR;
}

int ttrek_RegistryReadHTTP(const char *url, Tcl_DString *result) {
    CURL *curl = curl_easy_init();
    if (!curl) { return fail(result, "Could not initialize the registry connection"); }
    ttrek_CurlConfigureCA(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
    curl_easy_setopt(curl, CURLOPT_HEADEROPT, (long)CURLHEADER_SEPARATE);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ttrek/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, result);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    // Never print response bodies or Location headers, which can reflect secrets.
    if (status == 401 || status == 403) {
        return fail(result, status == 401 ? "Registry requires authentication (HTTP 401)" :
                                           "Registry access denied (HTTP 403)");
    }
    if (status == 404) {
        Tcl_DStringSetLength(result, 0);
        return TTREK_REGISTRY_NOT_FOUND;
    }
    if (status >= 300 && status < 400) {
        return fail(result, "Registry redirected the request; configure its final API URL with -registry");
    }
    if (code != CURLE_OK) {
        fail(result, "Could not read registry: ");
        Tcl_DStringAppend(result, curl_easy_strerror(code), -1);
        return TCL_ERROR;
    }
    if (status < 200 || status >= 300) { return fail(result, "Unexpected registry HTTP response"); }
    return TCL_OK;
}
