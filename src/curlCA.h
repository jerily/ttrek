#ifndef TTREK_CURL_CA_H
#define TTREK_CURL_CA_H

#include <curl/curl.h>
#include <stdlib.h>
#include <unistd.h>

static inline void ttrek_CurlConfigureCA(CURL *curl) {
#ifdef __linux__
    // A static libcurl otherwise remembers the builder's distro-specific CA
    // file. Select the runtime's trust store without disabling verification.
    const char *bundle = getenv("CURL_CA_BUNDLE");
    if (!bundle || !*bundle) { bundle = getenv("SSL_CERT_FILE"); }
    if (bundle && *bundle) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, bundle);
        curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
        return;
    }
    const char *paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",  // Debian, Ubuntu, Alpine
        "/etc/pki/tls/certs/ca-bundle.crt",   // RHEL, AlmaLinux, Amazon Linux
        "/etc/ssl/ca-bundle.pem",            // SUSE
        "/etc/ssl/cert.pem",                // Other OpenSSL distributions
        NULL
    };
    for (const char **path = paths; *path; ++path) {
        if (access(*path, R_OK) == 0) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, *path);
            return;
        }
    }
#else
    (void)curl;
#endif
}

#endif
