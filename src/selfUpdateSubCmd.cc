#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>
#include "curlCA.h"
#include <openssl/evp.h>
#include "subCmdDecls.h"
#include "semver/semver.h"

namespace {
namespace fs = std::filesystem;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

const char *target() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "macos-arm64";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "macos-x86_64";
#elif defined(__linux__) && defined(__aarch64__) && defined(__GLIBC__)
    return "linux-arm64-glibc";
#elif defined(__linux__) && defined(__x86_64__) && defined(__GLIBC__)
    return "linux-amd64-glibc";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-arm64-musl";
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-amd64-musl";
#else
    return nullptr;
#endif
}

std::string release_root() {
#ifdef TTREK_TEST_RELEASES
    const char *root = std::getenv("TTREK_TEST_RELEASE_ROOT");
    if (root) { return root; }
#endif
    return ttrek_IsLocalEnvironment() ? "http://localhost:8080" : "https://ttrek.sh";
}

std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> request(const std::string &url, long timeout_ms) {
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) { throw std::runtime_error("Cannot initialize download"); }
    ttrek_CurlConfigureCA(curl.get());
    const char *protocols = "https";
    if (ttrek_IsLocalEnvironment() && url.rfind("http://localhost:8080/", 0) == 0) {
        protocols = "http,https";
    }
    const char *redirect_protocols = "https";
#ifdef TTREK_TEST_RELEASES
    protocols = "http,https";
    redirect_protocols = "http,https";
#endif
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, protocols);
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, redirect_protocols);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, std::min(timeout_ms, 5000L));
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "ttrek/" XSTR(PROJECT_VERSION));
    return curl;
}

size_t metadata_bytes(char *bytes, size_t size, size_t count, void *data) {
    auto &buffer = *static_cast<std::string *>(data);
    if (count > 65536 || size > 65536 || size * count > 65536 - buffer.size()) { return 0; }
    try { buffer.append(bytes, size * count); } catch (...) { return 0; }
    return size * count;
}

struct Release {
    std::string version, url, sha256;
};

Release latest(long timeout_ms) {
    if (!target()) { throw std::runtime_error("No release distribution for this platform"); }
    const auto url = release_root() + "/releases/latest/" + target();
    auto curl = request(url, timeout_ms);
    std::string body;
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, metadata_bytes);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
    if (curl_easy_perform(curl.get()) != CURLE_OK) { throw std::runtime_error("Could not check ttrek.sh for a release"); }
    Json json(cJSON_ParseWithLengthOpts(body.c_str(), body.size() + 1, nullptr, 1), cJSON_Delete);
    auto field = [&](const char *name) -> std::string {
        auto *item = cJSON_GetObjectItemCaseSensitive(json.get(), name);
        if (!cJSON_IsString(item)) { throw std::runtime_error("Invalid release metadata"); }
        return item->valuestring;
    };
    Release release{field("version"), field("url"), field("sha256")};
    if (field("target") != target() ||
        !std::regex_match(release.version, std::regex("[0-9]{1,9}\\.[0-9]{1,9}\\.[0-9]{1,9}")) ||
        !std::regex_match(release.sha256, std::regex("[0-9a-f]{64}"))) {
        throw std::runtime_error("Invalid release version, platform or checksum");
    }
    return release;
}

bool newer(const std::string &version) {
    semver_t available{}, current{};
    if (semver_parse(version.c_str(), &available) || semver_parse(XSTR(PROJECT_VERSION), &current)) {
        semver_free(&available);
        semver_free(&current);
        throw std::runtime_error("Invalid release version");
    }
    bool result = semver_compare(available, current) > 0;
    semver_free(&available);
    semver_free(&current);
    return result;
}

struct Download {
    FILE *file;
    EVP_MD_CTX *hash;
    size_t bytes = 0;
};

size_t binary_bytes(char *bytes, size_t size, size_t count, void *data) {
    auto &download = *static_cast<Download *>(data);
    constexpr size_t maximum = 128 * 1024 * 1024;
    if (count > maximum || size > maximum || size * count > maximum - download.bytes) { return 0; }
    size_t length = size * count;
    if (EVP_DigestUpdate(download.hash, bytes, length) != 1 ||
        std::fwrite(bytes, 1, length, download.file) != length) { return 0; }
    download.bytes += length;
    return length;
}

void install(Tcl_Interp *interp, const Release &release) {
    const char *name = Tcl_GetNameOfExecutable();
    if (!name) { throw std::runtime_error("Cannot locate the running executable"); }
    const auto executable = fs::canonical(name);
    struct stat original{};
    if (stat(executable.c_str(), &original) || !S_ISREG(original.st_mode) ||
        (original.st_mode & (S_ISUID | S_ISGID))) {
        throw std::runtime_error("Cannot replace this executable");
    }
    std::string pattern = (executable.parent_path() / ".ttrek-update-XXXXXX").string();
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    int fd = mkstemp(temporary.data());
    if (fd < 0) { throw std::runtime_error("Cannot write beside the executable; check installation directory permissions"); }
    struct Cleanup {
        const char *path;
        ~Cleanup() { unlink(path); }
    } cleanup{temporary.data()};
    FILE *opened = fdopen(fd, "wb");
    if (!opened) { close(fd); throw std::runtime_error("Cannot open temporary executable"); }
    std::unique_ptr<FILE, decltype(&std::fclose)> file(opened, std::fclose);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!hash || EVP_DigestInit_ex(hash.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("Cannot initialize checksum verification");
    }
    Download download{file.get(), hash.get()};
    auto curl = request(release.url, 120000);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, binary_bytes);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &download);
    if (curl_easy_perform(curl.get()) != CURLE_OK || download.bytes == 0) {
        throw std::runtime_error("Release download failed; existing executable preserved");
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(hash.get(), digest, &length) != 1 || length != 32) {
        throw std::runtime_error("Cannot verify release checksum");
    }
    std::string hex;
    for (unsigned int i = 0; i < length; ++i) {
        hex += "0123456789abcdef"[digest[i] >> 4];
        hex += "0123456789abcdef"[digest[i] & 15];
    }
    if (hex != release.sha256) { throw std::runtime_error("Release checksum mismatch; existing executable preserved"); }
    if (std::fflush(file.get()) || fchmod(fd, original.st_mode & 0777) || fsync(fd)) {
        throw std::runtime_error("Cannot finish writing the release");
    }
    if (std::fclose(file.release())) { throw std::runtime_error("Cannot close the release file"); }
    // Check that the downloaded program runs on this host and reports the advertised version.
    const char *arguments[] = {temporary.data(), "self-update", "-version", "2>@1"};
    Tcl_Obj *output = Tcl_NewObj();
    Tcl_IncrRefCount(output);
    int status = ttrek_ExecuteCommand(interp, 4, arguments, output);
    std::string reported = Tcl_GetString(output);
    Tcl_DecrRefCount(output);
    if (status != TCL_OK || reported != release.version) {
        throw std::runtime_error("Downloaded executable failed its version check; existing executable preserved");
    }
    struct stat current{};
    if (stat(executable.c_str(), &current) || current.st_dev != original.st_dev || current.st_ino != original.st_ino) {
        throw std::runtime_error("Executable changed during download; retry self-update");
    }
    fs::rename(temporary.data(), executable);
    std::printf("Updated ttrek to %s at %s\n", release.version.c_str(), executable.c_str());
}
}

extern "C" int ttrek_SelfUpdateSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) try {
    if (objc == 2 && std::strcmp(Tcl_GetString(objv[1]), "-version") == 0) {
        std::puts(XSTR(PROJECT_VERSION));
        return TCL_OK;
    }
    bool check = objc == 2 && std::strcmp(Tcl_GetString(objv[1]), "-check") == 0;
    if (objc != 1 && !check) { throw std::runtime_error("Usage: ttrek self-update [-check|-version]"); }
    const auto release = latest(5000);
    if (!newer(release.version)) {
        std::puts("ttrek is up to date.");
    } else if (check) {
        std::printf("ttrek %s is available; run 'ttrek self-update' to upgrade.\n", release.version.c_str());
    } else {
        install(interp, release);
    }
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}

extern "C" void ttrek_CheckForUpdate(void) {
    // Never delay noninteractive commands or contaminate generated stdout scripts.
    if (!isatty(STDERR_FILENO) || std::getenv("TTREK_NO_UPDATE_CHECK")) { return; }
    try {
        const char *home = std::getenv("HOME");
        if (!home || !*home) { return; }
        const auto stamp = fs::path(home) / ".local/state/ttrek/update-check";
        if (fs::exists(stamp) && fs::file_time_type::clock::now() - fs::last_write_time(stamp) < std::chrono::hours(24)) { return; }
        fs::create_directories(stamp.parent_path());
        std::ofstream checked(stamp);
        if (!checked) { return; }
        checked << "checked\n";
        if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) { return; }
        try {
            const auto release = latest(1000);
            if (newer(release.version)) {
                std::fprintf(stderr, "ttrek %s is available; run 'ttrek self-update' to upgrade.\n", release.version.c_str());
            }
        } catch (...) { /* A release check must never fail the user's command. */ }
        curl_global_cleanup();
    } catch (...) { /* An unwritable cache must never fail the user's command. */ }
}
