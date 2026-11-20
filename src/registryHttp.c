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

static int request(const char *url, Tcl_DString *result, const char *body, int probe) {
    CURL *curl = curl_easy_init();
    if (!curl) { return fail(result, "Could not initialize the registry connection"); }
    ttrek_CurlConfigureCA(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
    curl_easy_setopt(curl, CURLOPT_HEADEROPT, (long)CURLHEADER_SEPARATE);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ttrek/" XSTR(PROJECT_VERSION));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, result);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    struct curl_slist *headers = NULL;
    if (body) {
        headers = curl_slist_append(NULL, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    }
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    // Never print response bodies or Location headers, which can reflect secrets.
    if (status == 401 || status == 403) {
        return fail(result, status == 401 ? "Registry requires authentication (HTTP 401)" :
                                           "Registry access denied (HTTP 403)");
    }
    if (body && status == 413) { return TTREK_REGISTRY_TOO_LARGE; }
    if (status == 404 || (probe && (status == 405 || status == 501))) {
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

int ttrek_RegistryReadHTTP(const char *url, Tcl_DString *result) {
    return request(url, result, NULL, 0);
}

int ttrek_RegistryProbeHTTP(const char *url, Tcl_DString *result) {
    return request(url, result, NULL, 1);
}

int ttrek_RegistryPostHTTP(const char *url, Tcl_DString *result, cJSON *body) {
    char *json = cJSON_PrintUnformatted(body);
    if (!json) { return fail(result, "Cannot serialize registry batch"); }
    int status = request(url, result, json, 0);
    cJSON_free(json);
    return status;
}
