#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <curl/curl.h>
#include "registry.h"
#include "base64.h"
#include <ttrie.h>
#include <ttrigrams.h>

namespace {
namespace fs = std::filesystem;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

void require(bool condition, const std::string &message) {
    if (!condition) { throw std::runtime_error(message); }
}

bool valid_component(const std::string &name) {
    return !name.empty() && name != "." && name != ".." &&
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-") == std::string::npos;
}

std::string read_file(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    require(file.is_open(), "Cannot read " + path.string());
    std::string data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    require(!file.bad(), "Cannot read " + path.string());
    return data;
}

Json read_spec(const fs::path &directory) {
    auto path = directory / SPEC_JSON_FILE;
    auto data = read_file(path);
    Json spec(cJSON_ParseWithLengthOpts(data.c_str(), data.size() + 1, nullptr, 1), cJSON_Delete);
    require(cJSON_IsObject(spec.get()), "Invalid JSON object in " + path.string());
    require(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(spec.get(), "dependencies")),
            "dependencies must be an object in " + path.string());
    return spec;
}

std::vector<fs::path> entries(const fs::path &directory) {
    std::vector<fs::path> paths;
    for (const auto &entry : fs::directory_iterator(directory)) {
        if (entry.path().filename().string().front() != '.') { paths.push_back(entry.path()); }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

int select_read_registry(Tcl_Interp *interp) {
    if (ttrek_RegistryHasSources()) { return ttrek_RegistryUseDefault(interp); }
    // Read-only commands work outside projects without creating state.
    auto project = fs::current_path();
    while (true) {
        if (fs::is_regular_file(project / SPEC_JSON_FILE)) {
            auto lock_path = project / LOCK_JSON_FILE;
            if (fs::exists(lock_path)) {
                auto data = read_file(lock_path);
                Json lock(cJSON_ParseWithLengthOpts(data.c_str(), data.size() + 1, nullptr, 1), cJSON_Delete);
                require(cJSON_IsObject(lock.get()), "Invalid JSON object in " + lock_path.string());
                if (cJSON_HasObjectItem(lock.get(), "registry") || cJSON_HasObjectItem(lock.get(), "registries")) {
                    Tcl_Obj *directory = Tcl_NewStringObj(project.c_str(), -1);
                    Tcl_IncrRefCount(directory);
                    int status = ttrek_RegistryLoadFromLock(interp, directory, lock.get());
                    Tcl_DecrRefCount(directory);
                    return status;
                }
            }
            break;
        }
        if (project == project.root_path()) { break; }
        project = project.parent_path();
    }
    return ttrek_RegistryUseDefault(interp);
}

std::string search_text(std::string text) {
    for (char &ch : text) { if (ch >= 'A' && ch <= 'Z') { ch += 'a' - 'A'; } }
    return text;
}

struct SearchEntry {
    std::string name;
    std::string normalized;
    std::string description;
    int tier = 8;
    int distance = 0;
    double coverage = 0;
    size_t unmatched = 0;
    int source = 0;
    bool remote = false;
};

struct SearchDocuments {
    std::vector<SearchEntry> &entries;
    std::vector<size_t> owners;
    std::vector<std::string> original;
    std::vector<std::string> normalized;
    std::vector<bool> tags;
};

bool search_order(const SearchEntry &a, const SearchEntry &b) {
    if (a.tier != b.tier) { return a.tier < b.tier; }
    if (a.distance != b.distance) { return a.distance < b.distance; }
    if (a.coverage != b.coverage) { return a.coverage > b.coverage; }
    if (a.unmatched != b.unmatched) { return a.unmatched < b.unmatched; }
    return a.name < b.name;
}

int collect_name_match(const char *key, int distance, void *context) {
    for (auto &entry : *static_cast<std::vector<SearchEntry> *>(context)) {
        if (!entry.remote && entry.tier > 5 && entry.normalized == key) {
            entry.tier = 5;
            entry.distance = distance;
        }
    }
    return 0;
}

int collect_document_match(const ttrigrams_match *match, void *context) noexcept {
    auto &documents = *static_cast<SearchDocuments *>(context);
    auto &entry = documents.entries[documents.owners[match->document]];
    int tier = (match->distance == 0 ? 3 : 6) + documents.tags[match->document];
    double coverage = match->query_trigrams ? double(match->shared_trigrams) / match->query_trigrams : 0;
    if (tier < entry.tier || (tier == entry.tier &&
        (match->distance < entry.distance || (match->distance == entry.distance && coverage > entry.coverage)))) {
        try { entry.description = documents.original[match->document]; }
        catch (...) { return -1; }
        entry.tier = tier;
        entry.distance = match->distance;
        entry.coverage = coverage;
    }
    return 0;
}

void collect_remote_matches(const char *base, int source, const std::string &query,
                            std::vector<SearchEntry> &found, std::set<std::string> &claimed) {
    std::unique_ptr<char, decltype(&curl_free)> encoded(curl_easy_escape(nullptr, query.c_str(), 0), curl_free);
    require(encoded != nullptr, "Cannot encode search query");
    const auto url = std::string(base) + "/search?q=" + encoded.get();
    Tcl_DString data;
    Tcl_DStringInit(&data);
    int status = ttrek_RegistryReadHTTP(url.c_str(), &data);
    std::string body = Tcl_DStringValue(&data);
    Tcl_DStringFree(&data);
    require(status != TTREK_REGISTRY_NOT_FOUND, "Registry does not provide a search endpoint: " + std::string(base));
    require(status == TCL_OK, body);
    Json result(cJSON_ParseWithLengthOpts(body.c_str(), body.size() + 1, nullptr, 1), cJSON_Delete);
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
        require(text.size() <= TTRIGRAMS_MAX_TEXT_LENGTH &&
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
        SearchEntry entry{name->valuestring, search_text(name->valuestring), text};
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
        entry.remote = true;
        found.push_back(std::move(entry));
    }
}

struct SearchMetadata {
    std::set<std::string> descriptions;
    std::set<std::string> tags;
};

void read_search_metadata(const fs::path &path, SearchMetadata &metadata) {
    auto data = read_file(path);
    Json spec(cJSON_ParseWithLengthOpts(data.c_str(), data.size() + 1, nullptr, 1), cJSON_Delete);
    require(cJSON_IsObject(spec.get()), "Invalid JSON object in " + path.string());
    auto *description = cJSON_GetObjectItemCaseSensitive(spec.get(), "description");
    if (description) {
        require(cJSON_IsString(description), "description must be a string in " + path.string());
        std::string text = description->valuestring;
        require(text.size() <= TTRIGRAMS_MAX_TEXT_LENGTH &&
                std::none_of(text.begin(), text.end(), [](unsigned char ch) { return ch < 32 || ch == 127; }),
                "description must be a single line of at most 4096 bytes in " + path.string());
        if (!text.empty()) { metadata.descriptions.insert(std::move(text)); }
    }
    auto *tags = cJSON_GetObjectItemCaseSensitive(spec.get(), "tags");
    if (!tags) { return; }
    require(cJSON_IsArray(tags), "tags must be an array of strings in " + path.string());
    const cJSON *tag;
    cJSON_ArrayForEach(tag, tags) {
        require(cJSON_IsString(tag), "tags must be an array of strings in " + path.string());
        std::string text = tag->valuestring;
        require(!text.empty() && text.size() <= 255 &&
                std::none_of(text.begin(), text.end(), [](unsigned char ch) { return ch < 32 || ch == 127; }),
                "tags must contain nonempty single-line strings of at most 255 bytes in " + path.string());
        metadata.tags.insert(std::move(text));
    }
}

Json versions(const fs::path &registry_directory, const std::string &package) {
    const auto directory = registry_directory / package;
    require(fs::exists(directory), "Package \"" + package + "\" was not found in registry \"" +
            registry_directory.string() + "\". Check the package name or use -registry DIRECTORY to select another registry.");
    require(fs::is_directory(directory), "Package path is not a directory: " + directory.string());
    Json result(cJSON_CreateObject(), cJSON_Delete);
    for (const auto &path : entries(directory)) {
        if (!fs::is_directory(path)) { continue; }
        require(valid_component(path.filename().string()), "Invalid version directory: " + path.string());
        auto spec = read_spec(path);
        cJSON_AddItemToObject(result.get(), path.filename().c_str(),
            cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(spec.get(), "dependencies"), 1));
    }
    return result;
}

void collect_use_flags(const cJSON *node, std::set<std::string> &flags) {
    if (cJSON_IsObject(node)) {
        auto *condition = cJSON_GetObjectItemCaseSensitive(node, "if");
        if (condition) {
            require(cJSON_IsString(condition), "Recipe 'if' must be a USE flag string");
            flags.insert(condition->valuestring);
        }
    }
    const cJSON *child;
    cJSON_ArrayForEach(child, node) { collect_use_flags(child, flags); }
}

Json resolve_scripts(const cJSON *scripts, const fs::path &directory) {
    Json result(cJSON_CreateObject(), cJSON_Delete);
    if (!scripts) { return result; }
    require(cJSON_IsObject(scripts), "recipe scripts must be an object");
    const cJSON *script;
    cJSON_ArrayForEach(script, scripts) {
        require(script->string && script->string[0], "recipe script names must not be empty");
        if (cJSON_IsString(script)) {
            cJSON_AddItemToObject(result.get(), script->string, cJSON_Duplicate(script, 1));
            continue;
        }
        auto *file = cJSON_GetObjectItemCaseSensitive(script, "file");
        auto *args = cJSON_GetObjectItemCaseSensitive(script, "args");
        require(cJSON_IsObject(script) && cJSON_GetArraySize(script) == (args ? 2 : 1) && cJSON_IsString(file),
                "recipe scripts must have string bodies or a file reference with optional args");
        require(!args || cJSON_IsArray(args), "recipe script args must be an array of strings");
        std::string presets;
        const cJSON *arg;
        cJSON_ArrayForEach(arg, args) {
            require(cJSON_IsString(arg), "recipe script args must be an array of strings");
            presets += " '";
            for (const char ch : std::string(arg->valuestring)) {
                presets += ch == '\'' ? "'\"'\"'" : std::string(1, ch);
            }
            presets += "'";
        }
        require(valid_component(file->valuestring), "Invalid recipe script filename");
        auto path = directory / file->valuestring;
        require(!fs::is_symlink(path) && fs::is_regular_file(path),
                "Recipe script must be a regular file in its recipe directory: " + path.string());
        auto body = read_file(path);
        require(body.find('\0') == std::string::npos, "Recipe script must not contain NUL bytes");
        if (!presets.empty()) {
            // Cache a self-contained shell body, preserving its interpreter and
            // placing literal presets before arguments supplied to `ttrek run`.
            size_t position = 0;
            if (body.rfind("#!", 0) == 0) {
                position = body.find('\n');
                if (position == std::string::npos) { position = body.size(); body += '\n'; }
                ++position;
            }
            body.insert(position, "set --" + presets + " \"$@\"\n");
        }
        cJSON_AddStringToObject(result.get(), script->string, body.c_str());
    }
    return result;
}

Json recipe(const fs::path &registry_directory, const std::vector<std::string> &parts) {
    const auto directory = registry_directory / parts[0] / parts[1];
    auto spec = read_spec(directory);
    auto *build = cJSON_GetObjectItemCaseSensitive(spec.get(), "build");
    std::string platform = parts[2] + "." + parts[3];
    for (char &ch : platform) { ch = std::tolower(static_cast<unsigned char>(ch)); }
    auto *steps = cJSON_GetObjectItemCaseSensitive(build, platform.c_str());
    if (!steps) { steps = cJSON_GetObjectItemCaseSensitive(build, "default"); }
    require(cJSON_IsArray(steps), "No build instruction array for " + platform + " in " + directory.string());

    Json result(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(result.get(), "version", parts[1].c_str());
    cJSON_AddItemToObject(result.get(), "dependencies",
        cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(spec.get(), "dependencies"), 1));
    cJSON_AddItemToObject(result.get(), "install_script", cJSON_Duplicate(steps, 1));
    auto scripts = resolve_scripts(cJSON_GetObjectItemCaseSensitive(spec.get(), "scripts"), directory);
    cJSON_AddItemToObject(result.get(), "scripts", scripts.release());
    auto *iuse = cJSON_AddArrayToObject(result.get(), "iuse");
    std::set<std::string> flags;
    collect_use_flags(spec.get(), flags);
    for (const auto &flag : flags) { cJSON_AddItemToArray(iuse, cJSON_CreateString(flag.c_str())); }

    auto *patches = cJSON_AddObjectToObject(result.get(), "patches");
    for (const auto &path : entries(directory)) {
        if (path.extension() != ".diff" || !fs::is_regular_file(path)) { continue; }
        require(valid_component(path.filename().string()), "Invalid patch filename: " + path.string());
        auto data = read_file(path);
        // Include space for the encoder's line breaks and terminator.
        std::vector<char> encoded(data.size() * 2 + 8);
        Tcl_Size length;
        base64_encode(data.data(), data.size(), encoded.data(), &length);
        cJSON_AddStringToObject(patches, path.filename().c_str(), encoded.data());
    }
    return result;
}
} // namespace

extern "C" int ttrek_RegistrySelectForRead(Tcl_Interp *interp) try {
    return select_read_registry(interp);
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}

extern "C" int ttrek_RegistrySearch(Tcl_Interp *interp, const char *query, Tcl_Obj *matches) try {
    std::string normalized = search_text(query);
    require(normalized.size() <= TTRIE_MAX_LENGTH && normalized.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyz0123456789._+- ") == std::string::npos,
        "Search query must contain 1 to 255 ASCII letters, digits, spaces, '.', '_', '+' or '-'");
    auto first = normalized.find_first_not_of(' ');
    require(first != std::string::npos, "Search query must not be empty");
    normalized = normalized.substr(first, normalized.find_last_not_of(' ') - first + 1);
    require(normalized != "." && normalized != "..", "Invalid search query");
    if (ttrek_RegistrySelectForRead(interp) != TCL_OK) { return TCL_ERROR; }
    // Search has historically accepted an existing public checkout even when
    // it was created manually rather than through ttrek sync.
    if (!ttrek_RegistryHasSources()) {
        const char *directory = ttrek_RegistryDefaultDirectory();
        if (directory && fs::is_directory(directory) &&
            ttrek_RegistryAddSource(interp, directory) != TCL_OK) { return TCL_ERROR; }
    }
    std::unique_ptr<ttrie, decltype(&ttrie_destroy)> trie(ttrie_create(), ttrie_destroy);
    require(trie != nullptr, "Cannot allocate recipe search trie");
    std::vector<SearchEntry> found;
    SearchDocuments documents{found, {}, {}, {}, {}};
    std::set<std::string> claimed;
    if (!ttrek_RegistryHasSources()) {
        collect_remote_matches(REGISTRY_URL, 0, normalized, found, claimed);
    }
    for (int source = 0; source < ttrek_RegistrySourceCount(); ++source) {
        if (const char *url = ttrek_RegistryHTTPUrl(source)) {
            collect_remote_matches(url, source, normalized, found, claimed);
            continue;
        }
        for (const auto &path : entries(ttrek_RegistryLocalDirectory(source))) {
            auto name = path.filename().string();
            if (!fs::is_directory(path) || !valid_component(name)) { continue; }
            if (!claimed.insert(name).second) { continue; }
            bool has_recipe = false;
            SearchMetadata metadata;
            for (const auto &version : entries(path)) {
                if (fs::is_directory(version) && valid_component(version.filename().string()) &&
                    fs::is_regular_file(version / SPEC_JSON_FILE)) {
                    has_recipe = true;
                    read_search_metadata(version / SPEC_JSON_FILE, metadata);
                }
            }
            if (!has_recipe) { continue; }
            SearchEntry entry{name, search_text(name), metadata.descriptions.empty() ? "" : *metadata.descriptions.begin()};
            entry.source = source;
            require(ttrie_insert(trie.get(), entry.normalized.c_str()) == 0, "Cannot index recipe: " + name);
            if (entry.normalized == normalized) { entry.tier = 0; }
            else if (entry.normalized.find(normalized) == 0) { entry.tier = 1; }
            else if (entry.normalized.find(normalized) != std::string::npos) { entry.tier = 2; }
            if (entry.tier == 1 || entry.tier == 2) { entry.unmatched = name.size() - normalized.size(); }
            for (const auto &text : metadata.descriptions) {
                documents.owners.push_back(found.size());
                documents.original.push_back(text);
                documents.normalized.push_back(search_text(text));
                documents.tags.push_back(false);
            }
            for (const auto &tag : metadata.tags) {
                documents.owners.push_back(found.size());
                documents.original.push_back(entry.description);
                documents.normalized.push_back(search_text(tag));
                documents.tags.push_back(true);
            }
            found.push_back(std::move(entry));
        }
    }
    std::vector<const char *> texts;
    for (const auto &text : documents.normalized) { texts.push_back(text.c_str()); }
    std::unique_ptr<ttrigrams, decltype(&ttrigrams_destroy)> index(
        ttrigrams_create(texts.data(), texts.size()), ttrigrams_destroy);
    require(index != nullptr, "Cannot allocate metadata search index");
    int limit = normalized.size() <= 3 ? 1 : 2;
    require(ttrie_search(trie.get(), normalized.c_str(), limit, collect_name_match, &found) == 0,
            "Recipe name search failed");
    require(ttrigrams_search(index.get(), normalized.c_str(), limit, collect_document_match, &documents) == 0,
            "Recipe metadata search failed");
    std::sort(found.begin(), found.end(), search_order);
    for (const auto &entry : found) {
        if (entry.tier == 8) { continue; }
        int owns;
        if (ttrek_RegistrySearchIsOwner(interp, entry.source, entry.name.c_str(), &owns) != TCL_OK) { return TCL_ERROR; }
        if (!owns) { continue; }
        Tcl_Obj *fields[] = {Tcl_NewStringObj(entry.name.c_str(), -1), Tcl_NewStringObj(entry.description.c_str(), -1)};
        if (Tcl_ListObjAppendElement(interp, matches, Tcl_NewListObj(2, fields)) != TCL_OK) { return TCL_ERROR; }
    }
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}

extern "C" int ttrek_RegistryReadLocal(const char *directory, const char *url, Tcl_DString *result) try {
    const std::string prefix = std::string(REGISTRY_URL) + "/";
    require(std::string(url).rfind(prefix, 0) == 0, "Unsupported local registry request: " + std::string(url));
    std::string suffix = url + prefix.size();
    std::vector<std::string> parts;
    size_t start = 0;
    do {
        auto end = suffix.find('/', start);
        auto part = suffix.substr(start, end == std::string::npos ? end : end - start);
        require(valid_component(part), "Invalid registry path component: " + part);
        parts.push_back(part);
        if (end == std::string::npos) { break; }
        start = end + 1;
    } while (true);
    require(parts.size() == 1 || parts.size() == 4, "Invalid registry request: " + suffix);
    const fs::path registry_directory(directory);
    require(fs::is_directory(registry_directory), "Registry directory is unavailable: " + registry_directory.string());
    if (!fs::exists(fs::symlink_status(registry_directory / parts[0]))) { return TTREK_REGISTRY_NOT_FOUND; }
    auto response = parts.size() == 1 ? versions(registry_directory, parts[0]) : recipe(registry_directory, parts);
    std::unique_ptr<char, decltype(&cJSON_free)> json(cJSON_PrintUnformatted(response.get()), cJSON_free);
    require(json != nullptr, "Cannot serialize local registry response");
    Tcl_DStringAppend(result, json.get(), -1);
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_DStringSetLength(result, 0);
    Tcl_DStringAppend(result, error.what(), -1);
    return TCL_ERROR;
}
