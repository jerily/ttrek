#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <git2.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <curl/curl.h>
#include "registry.h"

namespace {
namespace fs = std::filesystem;
struct Source {
    std::string location;
    std::string argument;
    bool http;
    bool public_registry = false;
};
std::vector<Source> sources;
std::map<std::string, size_t> owners;
bool implicit_source = false;
int include_public = 0;

void require(bool condition, const std::string &message) {
    if (!condition) { throw std::runtime_error(message); }
}

bool is_url(const std::string &value) { return value.find("://") != std::string::npos; }

std::string url_part(CURLU *url, CURLUPart part) {
    char *value = nullptr;
    const auto status = curl_url_get(url, part, &value, 0);
    std::unique_ptr<char, decltype(&curl_free)> owned(value, curl_free);
    return status == CURLUE_OK ? value : "";
}

std::string validate_url(const std::string &value) {
    // Diagnostics must never echo credentials supplied in a malformed URL.
    require(value.find_first_of("?#") == std::string::npos,
            "Registry URLs must not contain a query or fragment; use the registry API base URL");
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
    require(url && curl_url_set(url.get(), CURLUPART_URL, value.c_str(), CURLU_DISALLOW_USER) == CURLUE_OK,
            "Invalid registry URL; use an HTTP(S) API URL without embedded credentials");
    const auto scheme = url_part(url.get(), CURLUPART_SCHEME);
    require(scheme == "https" || scheme == "http", "Registry URLs must use HTTP or HTTPS");
    auto result = url_part(url.get(), CURLUPART_URL);
    while (!result.empty() && result.back() == '/') { result.pop_back(); }
    return result;
}

fs::path sync_stamp(const fs::path &directory) {
    auto stamp = directory / ".git/ttrek-last-sync";
    // A linked worktree has a .git file, not a metadata directory.
    std::error_code error;
    if (fs::is_regular_file(directory / ".git", error)) {
        git_libgit2_init();
        git_repository *repo = nullptr;
        if (git_repository_open_ext(&repo, directory.c_str(), GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr) == 0) {
            stamp = fs::path(git_repository_path(repo)) / "ttrek-last-sync";
        }
        git_repository_free(repo);
        git_libgit2_shutdown();
    }
    return stamp;
}

void warn_registry_freshness(const std::string &location) {
    static std::set<std::string> warned;
    if (warned.count(location)) { return; }
    std::error_code error;
    const auto stamp = sync_stamp(location);
    if (!fs::is_regular_file(stamp, error)) { return; } // Only ttrek-sync-managed checkouts.
    const auto modified = fs::last_write_time(stamp, error);
    const bool old = !error && fs::file_time_type::clock::now() - modified > std::chrono::hours(24);
    size_t ahead = 0, behind = 0;
    git_libgit2_init();
    git_repository *repo = nullptr;
    if (git_repository_open_ext(&repo, location.c_str(), GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr) == 0) {
        git_oid head, upstream;
        if (git_reference_name_to_id(&head, repo, "HEAD") == 0 &&
            git_reference_name_to_id(&upstream, repo, "refs/remotes/origin/main") == 0) {
            if (git_graph_ahead_behind(&ahead, &behind, repo, &head, &upstream) != 0) { behind = 0; }
        }
    }
    git_repository_free(repo);
    git_libgit2_shutdown();
    if (!old && !behind) { return; }
    warned.insert(location);
    const char *default_directory = ttrek_RegistryDefaultDirectory();
    const bool is_default = default_directory && fs::equivalent(location, default_directory, error);
    if (behind) {
        std::fprintf(stderr, "Local registry %s is behind the last fetched origin/main by %zu commit(s); ", location.c_str(), behind);
    } else {
        std::fprintf(stderr, "Local registry %s may be out of date (last synced over 24 hours ago); ", location.c_str());
    }
    std::fprintf(stderr, is_default ? "run 'ttrek sync' to refresh it.\n" :
        "run 'ttrek sync .' from that directory to refresh it.\n");
}

void add_source(const std::string &argument, const fs::path &project = {}) {
    require(!argument.empty(), "-registry requires a directory or API URL");
    const bool http = is_url(argument);
    const auto location = http ? validate_url(argument) : fs::canonical(project / argument).string();
    if (!http) { require(fs::is_directory(location), "Registry is not a directory: " + location); }
    sources.push_back({location, http ? location : argument, http});
    implicit_source = false;
    owners.clear();
    if (!http) { warn_registry_freshness(location); }
}

int read_source(const Source &source, const std::string &suffix, Tcl_DString *result) {
    Tcl_DStringSetLength(result, 0);
    if (source.http) {
        int status = ttrek_RegistryReadHTTP((source.location + "/" + suffix).c_str(), result);
        if (status == TCL_ERROR) {
            Tcl_DStringAppend(result, (" (registry \"" + source.location + "\")").c_str(), -1);
        }
        return status;
    }
    return ttrek_RegistryReadLocal(source.location.c_str(), (std::string(REGISTRY_URL) + "/" + suffix).c_str(), result);
}

std::string missing_package(const std::string &name) {
    std::string message = "Package \"" + name + "\" was not found in ";
    message += sources.size() == 1 ? "registry " : "registries ";
    for (size_t i = 0; i < sources.size(); ++i) {
        if (i) { message += ", "; }
        message += "\"" + sources[i].location + "\"";
    }
    return message + ". Check the package name or use -registry " +
        (ttrek_RegistryIsLocal() ? "DIRECTORY" : "SOURCE") + " to select another registry.";
}
}

extern "C" int ttrek_RegistryAddSource(Tcl_Interp *interp, const char *source) try {
    add_source(source ? source : "");
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}

extern "C" Tcl_Size ttrek_RegistryOption(void *, Tcl_Interp *interp, Tcl_Size objc,
                                         Tcl_Obj *const *objv, void *) {
    if (objc == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("-registry requires a directory or API URL", -1));
        return -1;
    }
    int status = ttrek_RegistryAddSource(interp, Tcl_GetString(objv[0]));
    return status == TCL_OK ? 1 : -1;
}

extern "C" void *ttrek_RegistryOptionHandler(void) {
    // Tcl_ArgvInfo represents its callback as an untyped pointer.
    return reinterpret_cast<void *>(ttrek_RegistryOption);
}

extern "C" int *ttrek_RegistryPublicOption(void) { return &include_public; }

extern "C" int ttrek_RegistryHasSources(void) { return !sources.empty(); }
extern "C" int ttrek_RegistryIsLocal(void) {
    return !sources.empty() && std::none_of(sources.begin(), sources.end(), [](const Source &s) { return s.http; });
}
extern "C" int ttrek_RegistrySourceCount(void) { return static_cast<int>(sources.size()); }
extern "C" const char *ttrek_RegistryLocalDirectory(int index) {
    return sources.at(index).http ? nullptr : sources.at(index).location.c_str();
}

extern "C" const char *ttrek_RegistryDefaultDirectory(void) {
    const char *home = std::getenv("HOME");
    static const std::string path = home && *home ? (fs::path(home) / ".local/ttrek-registry").string() : "";
    return path.empty() ? nullptr : path.c_str();
}

extern "C" int ttrek_RegistryUseDefault(Tcl_Interp *interp) try {
    const bool append_public = include_public && std::none_of(sources.begin(), sources.end(),
        [](const Source &source) { return source.public_registry; });
    if (!sources.empty() && !append_public) { return TCL_OK; }
    const char *directory = ttrek_RegistryDefaultDirectory();
    std::error_code error;
    const bool synced = directory && fs::is_regular_file(sync_stamp(directory), error);
    if (append_public) {
        add_source(synced ? directory : REGISTRY_URL);
        sources.back().public_registry = true;
    } else if (synced) {
        add_source(directory);
        implicit_source = true;
    }
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}

extern "C" int ttrek_RegistryLoadFromLock(Tcl_Interp *interp, Tcl_Obj *project, const cJSON *lock) try {
    if (!sources.empty()) { return ttrek_RegistryUseDefault(interp); }
    const auto *legacy = cJSON_GetObjectItemCaseSensitive(lock, "registry");
    const auto *list = cJSON_GetObjectItemCaseSensitive(lock, "registries");
    require(!legacy || !list, "Lock file must not contain both registry and registries");
    if (legacy) {
        require(cJSON_IsString(legacy) && legacy->valuestring[0], "Lock file registry must be a nonempty directory or URL string");
        add_source(legacy->valuestring, Tcl_GetString(project));
    } else if (list) {
        require(cJSON_IsArray(list) && cJSON_GetArraySize(list), "Lock file registries must be a nonempty array");
        const cJSON *item;
        cJSON_ArrayForEach(item, list) {
            const auto *public_registry = cJSON_GetObjectItemCaseSensitive(item, "public");
            if (public_registry) {
                require(cJSON_IsObject(item) && cJSON_GetArraySize(item) == 1 &&
                        cJSON_IsTrue(public_registry) && !item->next,
                        "Public registry must be the final entry, with only public set to true");
                include_public = 1;
                continue;
            }
            const auto *directory = cJSON_GetObjectItemCaseSensitive(item, "directory");
            const auto *url = cJSON_GetObjectItemCaseSensitive(item, "url");
            const auto *source = directory ? directory : url;
            require(cJSON_IsObject(item) && !(directory && url) && cJSON_IsString(source) && source->valuestring[0],
                    "Each registry must have one nonempty directory or url");
            require((url != nullptr) == is_url(source->valuestring), "Registry directory and URL fields must match their source type");
            require(cJSON_GetArraySize(item) == 1, "Unknown registry setting; store only directory or url");
            add_source(source->valuestring, Tcl_GetString(project));
        }
    }
    return ttrek_RegistryUseDefault(interp);
} catch (const std::exception &error) {
    sources.clear();
    owners.clear();
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}

extern "C" void ttrek_RegistrySaveConfiguration(cJSON *lock) {
    if (implicit_source || sources.empty()) { return; }
    if (sources.size() == 1 && !sources[0].http && !sources[0].public_registry) {
        cJSON_AddStringToObject(lock, "registry", sources[0].argument.c_str());
        return;
    }
    auto *list = cJSON_AddArrayToObject(lock, "registries");
    for (const auto &source : sources) {
        auto *entry = cJSON_CreateObject();
        if (source.public_registry) {
            cJSON_AddBoolToObject(entry, "public", 1);
        } else {
            cJSON_AddStringToObject(entry, source.http ? "url" : "directory", source.argument.c_str());
        }
        cJSON_AddItemToArray(list, entry);
    }
}

extern "C" int ttrek_RegistryGetConfigured(const char *url, Tcl_DString *result) try {
    const std::string prefix = std::string(REGISTRY_URL) + "/";
    require(std::string(url).rfind(prefix, 0) == 0, "Unsupported registry request");
    const std::string suffix = url + prefix.size();
    size_t start = 0, count = 0;
    do {
        auto end = suffix.find('/', start);
        auto part = suffix.substr(start, end == std::string::npos ? end : end - start);
        require(!part.empty() && part != "." && part != ".." && part.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-") == std::string::npos,
                "Invalid registry path component: " + part);
        ++count;
        if (end == std::string::npos) { break; }
        start = end + 1;
    } while (true);
    require(count == 1 || count == 4, "Invalid registry request");
    const auto package = suffix.substr(0, suffix.find('/'));
    auto owner = owners.find(package);
    if (owner == owners.end()) {
        for (size_t i = 0; i < sources.size(); ++i) {
            int status = read_source(sources[i], package, result);
            if (status == TTREK_REGISTRY_NOT_FOUND) { continue; }
            if (status != TCL_OK) { return TCL_ERROR; }
            std::unique_ptr<cJSON, decltype(&cJSON_Delete)> metadata(cJSON_ParseWithLengthOpts(
                Tcl_DStringValue(result), Tcl_DStringLength(result) + 1, nullptr, 1), cJSON_Delete);
            require(cJSON_IsObject(metadata.get()), "Invalid registry metadata for " + package +
                    " in registry \"" + sources[i].location + "\"");
            owners[package] = i;
            if (count == 1) { return TCL_OK; }
            owner = owners.find(package);
            break;
        }
        require(owner != owners.end(), missing_package(package));
    }
    int status = read_source(sources[owner->second], suffix, result);
    if (status == TTREK_REGISTRY_NOT_FOUND) {
        throw std::runtime_error("Package or build recipe \"" + suffix + "\" was not found in registry \"" +
                                 sources[owner->second].location + "\"");
    }
    return status;
} catch (const std::exception &error) {
    Tcl_DStringSetLength(result, 0);
    Tcl_DStringAppend(result, error.what(), -1);
    return TCL_ERROR;
}
