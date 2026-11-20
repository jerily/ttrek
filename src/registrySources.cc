#include <algorithm>
#include <chrono>
#include <cmath>
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
#include "registryBatch.h"

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
std::map<std::string, bool> batch_support;
std::map<std::string, std::string> plan_payloads;
bool implicit_source = false;
bool websites_only = false;
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
    if (websites_only && !http) {
        // Saved registries can contain directories for package installation.
        if (!project.empty()) { return; }
        throw std::runtime_error("ttrek search -registry requires an HTTP(S) registry API URL supporting GET BASE/search?q=QUERY");
    }
    const auto location = http ? validate_url(argument) : fs::canonical(project / argument).string();
    if (!http) {
        require(fs::is_directory(location), "Registry is not a directory: " + location);
        warn_registry_freshness(location);
    }
    sources.push_back({location, http ? location : argument, http});
    implicit_source = false;
    owners.clear();
    plan_payloads.clear();
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

bool valid_component(const std::string &name) {
    return !name.empty() && name != "." && name != ".." &&
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-") == std::string::npos;
}

std::string search_text(std::string text) {
    for (char &ch : text) { if (ch >= 'A' && ch <= 'Z') { ch += 'a' - 'A'; } }
    return text;
}

struct SearchEntry {
    std::string name;
    std::string description;
    int tier = 8;
    int distance = 0;
    double coverage = 0;
    size_t unmatched = 0;
    size_t source = 0;
    std::string languages = {};
};

bool search_order(const SearchEntry &a, const SearchEntry &b) {
    if (a.tier != b.tier) { return a.tier < b.tier; }
    if (a.distance != b.distance) { return a.distance < b.distance; }
    if (a.coverage != b.coverage) { return a.coverage > b.coverage; }
    if (a.unmatched != b.unmatched) { return a.unmatched < b.unmatched; }
    return a.name < b.name;
}

void collect_remote_matches(const std::string &base, size_t source, const std::string &query,
                            std::vector<SearchEntry> &found, std::set<std::string> &claimed) {
    std::unique_ptr<char, decltype(&curl_free)> encoded(curl_easy_escape(nullptr, query.c_str(), 0), curl_free);
    require(encoded != nullptr, "Cannot encode search query");
    const auto url = base + "/search?q=" + encoded.get();
    Tcl_DString data;
    Tcl_DStringInit(&data);
    int status = ttrek_RegistryReadHTTP(url.c_str(), &data);
    std::string body = Tcl_DStringValue(&data);
    Tcl_DStringFree(&data);
    require(status != TTREK_REGISTRY_NOT_FOUND, "Registry does not provide a search endpoint: " + base);
    require(status == TCL_OK, body);
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> result(
        cJSON_ParseWithLengthOpts(body.c_str(), body.size() + 1, nullptr, 1), cJSON_Delete);
    require(cJSON_IsArray(result.get()), "Invalid registry search response");
    const std::vector<std::string> kinds{"exact", "prefix", "substring", "description", "fuzzy", "description-fuzzy"};
    const cJSON *row;
    cJSON_ArrayForEach(row, result.get()) {
        auto *name = cJSON_GetObjectItemCaseSensitive(row, "name");
        auto *description = cJSON_GetObjectItemCaseSensitive(row, "description");
        auto *kind = cJSON_GetObjectItemCaseSensitive(row, "match");
        auto *distance = cJSON_GetObjectItemCaseSensitive(row, "distance");
        auto *coverage = cJSON_GetObjectItemCaseSensitive(row, "coverage");
        require(cJSON_IsString(name) && valid_component(name->valuestring) &&
                cJSON_IsString(description) && cJSON_IsString(kind), "Invalid registry search result");
        std::string text = description->valuestring;
        require(text.size() <= 4096 &&
                std::none_of(text.begin(), text.end(), [](unsigned char ch) { return ch < 32 || ch == 127; }),
                "Invalid registry search description");
        auto tier = std::find(kinds.begin(), kinds.end(), kind->valuestring);
        require(tier != kinds.end(), "Invalid registry search match type");
        require(!distance || (cJSON_IsNumber(distance) && distance->valuedouble >= 0 &&
                distance->valuedouble <= 2 && distance->valuedouble == distance->valueint),
                "Invalid registry search distance");
        require(!coverage || (cJSON_IsNumber(coverage) && std::isfinite(coverage->valuedouble) &&
                coverage->valuedouble >= 0 && coverage->valuedouble <= 1), "Invalid registry search coverage");
        if (!claimed.insert(name->valuestring).second) { continue; }
        SearchEntry entry{name->valuestring, text};
        const auto *languages = cJSON_GetObjectItemCaseSensitive(row, "languages");
        require(ttrek_LanguagesValid(languages), "Invalid registry search languages");
        Tcl_Obj *label = ttrek_LanguagesLabel(languages);
        Tcl_IncrRefCount(label);
        entry.languages = Tcl_GetString(label);
        Tcl_DecrRefCount(label);
        entry.tier = tier - kinds.begin();
        if (entry.tier >= 4) { ++entry.tier; }
        // Keep the HTTP match vocabulary compatible with older clients.
        // New servers identify tag hits with an optional field discriminator.
        auto *field = cJSON_GetObjectItemCaseSensitive(row, "field");
        if (field) {
            require(cJSON_IsString(field) && std::string(field->valuestring) == "tags" &&
                    (entry.tier == 3 || entry.tier == 6), "Invalid registry search field");
            ++entry.tier;
        }
        entry.distance = distance ? distance->valueint : 0;
        entry.coverage = coverage ? coverage->valuedouble : 0;
        if (entry.tier == 1 || entry.tier == 2) {
            require(entry.name.size() >= query.size(), "Invalid registry search name match");
            entry.unmatched = entry.name.size() - query.size();
        }
        entry.source = source;
        found.push_back(std::move(entry));
    }
}

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

struct Response {
    Tcl_DString data;
    Response() { Tcl_DStringInit(&data); }
    ~Response() { Tcl_DStringFree(&data); }
    std::string text() { return std::string(Tcl_DStringValue(&data), Tcl_DStringLength(&data)); }
    Json json() {
        return Json(cJSON_ParseWithLengthOpts(Tcl_DStringValue(&data), Tcl_DStringLength(&data) + 1, nullptr, 1),
                    cJSON_Delete);
    }
};

std::string encode(const cJSON *item) {
    std::unique_ptr<char, decltype(&cJSON_free)> raw(cJSON_PrintUnformatted(item), cJSON_free);
    require(raw != nullptr, "Cannot serialize registry result");
    return raw.get();
}

bool supports_batch(const Source &source) {
    if (!source.http) { return false; }
    auto known = batch_support.find(source.location);
    if (known != batch_support.end()) { return known->second; }
    Response reply;
    const auto url = source.location + "/batch/capabilities";
    const auto status = sources.empty() ? ttrek_RegistryPublicBatchRequest(url.c_str(), &reply.data, nullptr)
                                        : ttrek_RegistryProbeHTTP(url.c_str(), &reply.data);
    require(status == TCL_OK || status == TTREK_REGISTRY_NOT_FOUND,
            "Could not check batch capabilities at " + source.location + ": " + reply.text());
    // Capability discovery never establishes ownership. Older registries may
    // use catch-all handlers; only a positive version advertisement opts in.
    auto document = status == TCL_OK ? reply.json() : Json(nullptr, cJSON_Delete);
    auto *version = cJSON_GetObjectItemCaseSensitive(document.get(), "batch");
    return batch_support[source.location] = cJSON_IsNumber(version) && version->valuedouble == 1;
}

struct BatchTooLarge {};

// Keep normal requests full-sized. On a size rejection, retry only the
// rejected range. Completed requests can already have recorded downloads.
template <typename Request>
void for_batches(size_t count, Request request) {
    size_t start = 0, limit = TTREK_REGISTRY_BATCH_LIMIT;
    while (start < count) {
        const auto end = std::min(count, start + limit);
        try {
            request(start, end);
            start = end;
        } catch (const BatchTooLarge &) {
            require(end - start > 1, "Registry cannot serve a single batch item within its size limit");
            limit = (end - start) / 2;
        }
    }
}

Json post_batch(const Source &source, const char *path, cJSON *request, size_t expected) {
    Response reply;
    const auto url = source.location + path;
    const auto status = sources.empty() ? ttrek_RegistryPublicBatchRequest(url.c_str(), &reply.data, request)
                                        : ttrek_RegistryPostHTTP(url.c_str(), &reply.data, request);
    if (status == TTREK_REGISTRY_TOO_LARGE) { throw BatchTooLarge{}; }
    if (status != TCL_OK) {
        throw std::runtime_error("Registry batch failed at " + source.location + ": " + reply.text());
    }
    auto result = reply.json();
    require(cJSON_IsObject(result.get()) && size_t(cJSON_GetArraySize(result.get())) == expected,
            "Invalid registry batch response from " + source.location);
    return result;
}

using PackageRows = std::map<std::string, std::string>;

PackageRows read_packages(const Source &source, const std::vector<std::string> &names, bool existence) {
    PackageRows found;
    if (names.empty()) { return found; }
    if (!supports_batch(source)) {
        for (const auto &name : names) {
            Response reply;
            int status = sources.empty()
                ? ttrek_RegistryGet((source.location + "/" + name).c_str(), &reply.data, nullptr)
                : read_source(source, name, &reply.data);
            if (status == TTREK_REGISTRY_NOT_FOUND) { continue; }
            if (status != TCL_OK) {
                auto message = reply.text();
                throw std::runtime_error(message.empty() ? "Could not read registry metadata for " + name : message);
            }
            auto versions = reply.json();
            require(cJSON_IsObject(versions.get()), "Invalid registry metadata for " + name + " in " + source.location);
            found[name] = existence ? "" : reply.text();
        }
        return found;
    }
    for_batches(names.size(), [&](size_t start, size_t end) {
        Json request(cJSON_CreateObject(), cJSON_Delete);
        cJSON_AddStringToObject(request.get(), "mode", existence ? "exists" : "versions");
        auto *items = cJSON_AddArrayToObject(request.get(), "names");
        for (size_t i = start; i < end; ++i) { cJSON_AddItemToArray(items, cJSON_CreateString(names[i].c_str())); }
        auto result = post_batch(source, "/batch/packages", request.get(), end - start);
        for (size_t i = start; i < end; ++i) {
            const auto &name = names[i];
            auto *row = cJSON_GetObjectItemCaseSensitive(result.get(), name.c_str());
            if (cJSON_IsFalse(row)) { continue; }
            require(existence ? cJSON_IsTrue(row) : cJSON_IsObject(row), "Invalid registry batch entry for " + name);
            found[name] = existence ? "" : encode(row);
        }
    });
    return found;
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
        Tcl_SetObjResult(interp, Tcl_NewStringObj(websites_only ? "ttrek search -registry requires an HTTP(S) registry API URL" :
            "-registry requires a directory or API URL", -1));
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
extern "C" void ttrek_RegistryUseWebsitesForSearch(void) { websites_only = true; }

extern "C" const char *ttrek_RegistryDefaultDirectory(void) {
    const char *home = std::getenv("HOME");
    static const std::string path = home && *home ? (fs::path(home) / ".local/ttrek-registry").string() : "";
    return path.empty() ? nullptr : path.c_str();
}

extern "C" int ttrek_RegistryUseDefault(Tcl_Interp *interp) try {
    if (websites_only) {
        const auto public_url = std::string(REGISTRY_URL);
        if (sources.empty() || (include_public && std::none_of(sources.begin(), sources.end(),
                [&](const Source &s) { return s.location == public_url; }))) {
            add_source(public_url);
        }
        return TCL_OK;
    }
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
        require(valid_component(part), "Invalid registry path component: " + part);
        ++count;
        if (end == std::string::npos) { break; }
        start = end + 1;
    } while (true);
    require(count == 1 || count == 4 || (count == 3 && suffix.substr(suffix.rfind('/') + 1) == "languages"),
            "Invalid registry request");
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

extern "C" int ttrek_RegistrySearch(Tcl_Interp *interp, const char *query, Tcl_Obj *matches) try {
    std::string normalized = search_text(query);
    require(normalized.size() <= 255 && normalized.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyz0123456789._+- ") == std::string::npos,
        "Search query must contain 1 to 255 ASCII letters, digits, spaces, '.', '_', '+' or '-'");
    auto first = normalized.find_first_not_of(' ');
    require(first != std::string::npos, "Search query must not be empty");
    normalized = normalized.substr(first, normalized.find_last_not_of(' ') - first + 1);
    require(normalized != "." && normalized != "..", "Invalid search query");
    if (ttrek_RegistrySelectForRead(interp) != TCL_OK) { return TCL_ERROR; }
    std::vector<SearchEntry> found;
    std::set<std::string> claimed;
    for (size_t source = 0; source < sources.size(); ++source) {
        collect_remote_matches(sources[source].location, source, normalized, found, claimed);
    }
    std::set<std::string> hidden;
    for (size_t i = 0; i + 1 < sources.size(); ++i) {
        std::vector<std::string> names;
        for (const auto &entry : found) {
            if (entry.source > i && !hidden.count(entry.name)) { names.push_back(entry.name); }
        }
        for (const auto &row : read_packages(sources[i], names, true)) { hidden.insert(row.first); }
    }
    std::sort(found.begin(), found.end(), search_order);
    for (const auto &entry : found) {
        if (hidden.count(entry.name)) { continue; }
        Tcl_Obj *fields[] = {Tcl_NewStringObj(entry.name.c_str(), -1), Tcl_NewStringObj(entry.description.c_str(), -1),
                            Tcl_NewStringObj(entry.languages.c_str(), -1)};
        if (Tcl_ListObjAppendElement(interp, matches, Tcl_NewListObj(3, fields)) != TCL_OK) { return TCL_ERROR; }
    }
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}

std::string ttrek_MissingPackage(const std::string &name) {
    return sources.empty() ? "Package \"" + name + "\" was not found in " + REGISTRY_URL : missing_package(name);
}

PackageRows ttrek_RegistryPackageBatch(const std::set<std::string> &names) {
    for (const auto &name : names) { require(valid_component(name), "Invalid registry path component: " + name); }
    std::vector<std::string> remaining(names.begin(), names.end());
    if (sources.empty()) {
        auto result = read_packages({REGISTRY_URL, "", true}, remaining, false);
        // The implicit public GET returns an empty version map for a miss.
        // Let the solver backtrack if an alternative version has other dependencies.
        for (const auto &name : remaining) { result.try_emplace(name, "{}"); }
        return result;
    }
    PackageRows result;
    for (size_t i = 0; i < sources.size() && !remaining.empty(); ++i) {
        auto rows = read_packages(sources[i], remaining, false);
        for (const auto &row : rows) { owners[row.first] = i; }
        result.merge(rows);
        remaining.erase(std::remove_if(remaining.begin(), remaining.end(),
            [&](const auto &name) { return result.count(name); }), remaining.end());
    }
    return result;
}

void ttrek_RegistryPlanBatch(const std::vector<RegistryPlanItem> &items, const char *os, const char *arch) {
    plan_payloads.clear();
    std::map<size_t, std::vector<RegistryPlanItem>> groups;
    for (const auto &item : items) {
        require(valid_component(item.name) && valid_component(item.version), "Invalid package name or version");
        groups[sources.empty() ? 0 : owners.at(item.name)].push_back(item);
    }
    const Source default_source{REGISTRY_URL, "", true};
    for (const auto &[owner, entries] : groups) {
        const auto &source = sources.empty() ? default_source : sources[owner];
        if (!supports_batch(source)) { continue; }
        for_batches(entries.size(), [&](size_t start, size_t end) {
            Json request(cJSON_CreateObject(), cJSON_Delete);
            cJSON_AddStringToObject(request.get(), "os", os);
            cJSON_AddStringToObject(request.get(), "arch", arch);
            auto *array = cJSON_AddArrayToObject(request.get(), "items");
            bool needs_recipe = false;
            for (size_t i = start; i < end; ++i) {
                const auto &entry = entries[i];
                auto *item = cJSON_CreateObject();
                cJSON_AddItemToArray(array, item);
                cJSON_AddStringToObject(item, "name", entry.name.c_str());
                cJSON_AddStringToObject(item, "version", entry.version.c_str());
                cJSON_AddStringToObject(item, "field", entry.recipe ? "recipe" : "languages");
                needs_recipe |= entry.recipe;
            }
            Json result(nullptr, cJSON_Delete);
            try {
                result = post_batch(source, "/batch/plan", request.get(), end - start);
            } catch (const BatchTooLarge &) {
                if (needs_recipe || end - start > 1) { throw; }
            } catch (const std::exception &) {
                if (needs_recipe) { throw; }
                // Display-only metadata is optional, also for unavailable registries.
            }
            for (size_t i = start; i < end; ++i) {
                const auto &entry = entries[i];
                auto key = entry.name + "@" + entry.version;
                const auto prefix = std::string(REGISTRY_URL) + "/" + entry.name + "/" + entry.version + "/";
                auto *row = cJSON_GetObjectItemCaseSensitive(result.get(), key.c_str());
                if (entry.recipe) {
                    auto *version = cJSON_GetObjectItemCaseSensitive(row, "version");
                    require(cJSON_IsString(version) && entry.version == version->valuestring,
                            "Missing or invalid build recipe for " + key + " in " + source.location);
                    plan_payloads[prefix + os + "/" + arch] = encode(row);
                    row = cJSON_GetObjectItemCaseSensitive(row, "languages");
                }
                plan_payloads[prefix + "languages"] = cJSON_IsArray(row) ? encode(row) : "null";
            }
        });
    }
}

extern "C" int ttrek_RegistryPlanRead(const char *url, Tcl_DString *result) {
    auto item = plan_payloads.find(url);
    if (item == plan_payloads.end()) { return ttrek_RegistryGet(url, result, nullptr); }
    Tcl_DStringAppend(result, item->second.c_str(), item->second.size());
    plan_payloads.erase(item);
    return TCL_OK;
}

extern "C" int ttrek_RegistryUsesBatchTelemetry(void) {
    auto support = batch_support.find(REGISTRY_URL);
    return sources.empty() && support != batch_support.end() && support->second;
}
