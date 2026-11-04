#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "registry.h"
#include "base64.h"
#include <ttrie.h>
#include <ttrigrams.h>

namespace {
namespace fs = std::filesystem;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
fs::path registry_directory;
std::string registry_argument;
bool registry_is_default = false;

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

int select_search_registry(Tcl_Interp *interp) {
    if (ttrek_RegistryIsLocal()) { return TCL_OK; }
    // Search works outside projects and never creates project state or directories.
    auto project = fs::current_path();
    while (true) {
        if (fs::is_regular_file(project / SPEC_JSON_FILE)) {
            auto lock_path = project / LOCK_JSON_FILE;
            if (fs::exists(lock_path)) {
                auto data = read_file(lock_path);
                Json lock(cJSON_ParseWithLengthOpts(data.c_str(), data.size() + 1, nullptr, 1), cJSON_Delete);
                require(cJSON_IsObject(lock.get()), "Invalid JSON object in " + lock_path.string());
                auto *registry = cJSON_GetObjectItemCaseSensitive(lock.get(), "registry");
                if (registry) {
                    require(cJSON_IsString(registry) && registry->valuestring[0],
                            "Lock file registry must be a nonempty directory string");
                    return ttrek_RegistrySetDirectory(interp, (project / registry->valuestring).c_str());
                }
            }
            break;
        }
        if (project == project.root_path()) { break; }
        project = project.parent_path();
    }
    const char *directory = ttrek_RegistryDefaultDirectory();
    require(directory && fs::is_directory(directory),
            "No local registry; run 'ttrek sync' or use 'ttrek search -registry DIRECTORY NAME'");
    return ttrek_RegistrySetDirectory(interp, directory);
}

std::string search_text(std::string text) {
    for (char &ch : text) { if (ch >= 'A' && ch <= 'Z') { ch += 'a' - 'A'; } }
    return text;
}

struct SearchEntry {
    std::string name;
    std::string normalized;
    std::string description;
    int tier = 6;
    int distance = 0;
    size_t shared = 0;
    size_t unmatched = 0;
};

struct SearchDescriptions {
    std::vector<SearchEntry> &entries;
    std::vector<size_t> owners;
    std::vector<std::string> original;
    std::vector<std::string> normalized;
};

bool search_order(const SearchEntry &a, const SearchEntry &b) {
    if (a.tier != b.tier) { return a.tier < b.tier; }
    if (a.distance != b.distance) { return a.distance < b.distance; }
    if (a.shared != b.shared) { return a.shared > b.shared; }
    if (a.unmatched != b.unmatched) { return a.unmatched < b.unmatched; }
    return a.name < b.name;
}

int collect_name_match(const char *key, int distance, void *context) {
    for (auto &entry : *static_cast<std::vector<SearchEntry> *>(context)) {
        if (entry.tier > 4 && entry.normalized == key) {
            entry.tier = 4;
            entry.distance = distance;
        }
    }
    return 0;
}

int collect_description_match(const ttrigrams_match *match, void *context) noexcept {
    auto &descriptions = *static_cast<SearchDescriptions *>(context);
    auto &entry = descriptions.entries[descriptions.owners[match->document]];
    int tier = match->distance == 0 ? 3 : 5;
    if (tier < entry.tier || (tier == entry.tier &&
        (match->distance < entry.distance || (match->distance == entry.distance && match->shared_trigrams > entry.shared)))) {
        try { entry.description = descriptions.original[match->document]; }
        catch (...) { return -1; }
        entry.tier = tier;
        entry.distance = match->distance;
        entry.shared = match->shared_trigrams;
    }
    return 0;
}

std::string read_search_description(const fs::path &path) {
    auto data = read_file(path);
    Json spec(cJSON_ParseWithLengthOpts(data.c_str(), data.size() + 1, nullptr, 1), cJSON_Delete);
    require(cJSON_IsObject(spec.get()), "Invalid JSON object in " + path.string());
    auto *description = cJSON_GetObjectItemCaseSensitive(spec.get(), "description");
    if (!description) { return {}; }
    require(cJSON_IsString(description), "description must be a string in " + path.string());
    std::string text = description->valuestring;
    require(text.size() <= TTRIGRAMS_MAX_TEXT_LENGTH &&
            std::none_of(text.begin(), text.end(), [](unsigned char ch) { return ch < 32 || ch == 127; }),
            "description must be a single line of at most 4096 bytes in " + path.string());
    return text;
}

Json versions(const std::string &package) {
    Json result(cJSON_CreateObject(), cJSON_Delete);
    for (const auto &path : entries(registry_directory / package)) {
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
        require(cJSON_IsObject(script) && cJSON_GetArraySize(script) == 1 && cJSON_IsString(file),
                "recipe scripts must have string bodies or a single file reference");
        require(valid_component(file->valuestring), "Invalid recipe script filename");
        auto path = directory / file->valuestring;
        require(!fs::is_symlink(path) && fs::is_regular_file(path),
                "Recipe script must be a regular file in its recipe directory: " + path.string());
        auto body = read_file(path);
        require(body.find('\0') == std::string::npos, "Recipe script must not contain NUL bytes");
        cJSON_AddStringToObject(result.get(), script->string, body.c_str());
    }
    return result;
}

Json recipe(const std::vector<std::string> &parts) {
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

extern "C" int ttrek_RegistrySetDirectory(Tcl_Interp *interp, const char *directory) try {
    if (!directory) { return TCL_OK; }
    require(directory[0] != '\0', "-registry requires a directory");
    auto path = fs::canonical(directory);
    require(fs::is_directory(path), "Registry is not a directory: " + path.string());
    registry_directory = std::move(path);
    registry_argument = directory;
    registry_is_default = false;
    const char *default_directory = ttrek_RegistryDefaultDirectory();
    std::error_code error;
    if (default_directory && fs::equivalent(registry_directory, default_directory, error)) {
        const auto stamp = fs::path(default_directory) / ".git/ttrek-last-sync";
        if (fs::is_regular_file(stamp, error)) {
            const auto modified = fs::last_write_time(stamp, error);
            if (!error && fs::file_time_type::clock::now() - modified > std::chrono::hours(24)) {
                std::fprintf(stderr, "Local registry has not been synced in over 24 hours; run 'ttrek sync' to refresh %s.\n", default_directory);
            }
        }
    }
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}

extern "C" int ttrek_RegistryIsLocal(void) {
    return !registry_directory.empty();
}

extern "C" int ttrek_RegistrySearchLocal(Tcl_Interp *interp, const char *query, Tcl_Obj *matches) try {
    std::string normalized = search_text(query);
    require(normalized.size() <= TTRIE_MAX_LENGTH && normalized.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyz0123456789._+- ") == std::string::npos,
        "Search query must contain 1 to 255 ASCII letters, digits, spaces, '.', '_', '+' or '-'");
    auto first = normalized.find_first_not_of(' ');
    require(first != std::string::npos, "Search query must not be empty");
    normalized = normalized.substr(first, normalized.find_last_not_of(' ') - first + 1);
    require(normalized != "." && normalized != "..", "Invalid search query");
    if (select_search_registry(interp) != TCL_OK) { return TCL_ERROR; }
    std::unique_ptr<ttrie, decltype(&ttrie_destroy)> trie(ttrie_create(), ttrie_destroy);
    require(trie != nullptr, "Cannot allocate recipe search trie");
    std::vector<SearchEntry> found;
    SearchDescriptions descriptions{found, {}, {}, {}};
    for (const auto &path : entries(registry_directory)) {
        auto name = path.filename().string();
        if (!fs::is_directory(path) || !valid_component(name)) { continue; }
        bool has_recipe = false;
        std::set<std::string> texts;
        for (const auto &version : entries(path)) {
            if (fs::is_directory(version) && valid_component(version.filename().string()) &&
                fs::is_regular_file(version / SPEC_JSON_FILE)) {
                has_recipe = true;
                auto text = read_search_description(version / SPEC_JSON_FILE);
                if (!text.empty()) { texts.insert(std::move(text)); }
            }
        }
        if (!has_recipe) { continue; }
        SearchEntry entry{name, search_text(name), texts.empty() ? "" : *texts.begin()};
        require(ttrie_insert(trie.get(), entry.normalized.c_str()) == 0, "Cannot index recipe: " + name);
        if (entry.normalized == normalized) { entry.tier = 0; }
        else if (entry.normalized.find(normalized) == 0) { entry.tier = 1; }
        else if (entry.normalized.find(normalized) != std::string::npos) { entry.tier = 2; }
        if (entry.tier == 1 || entry.tier == 2) { entry.unmatched = name.size() - normalized.size(); }
        for (const auto &text : texts) {
            descriptions.owners.push_back(found.size());
            descriptions.original.push_back(text);
            descriptions.normalized.push_back(search_text(text));
        }
        found.push_back(std::move(entry));
    }
    std::vector<const char *> texts;
    for (const auto &text : descriptions.normalized) { texts.push_back(text.c_str()); }
    std::unique_ptr<ttrigrams, decltype(&ttrigrams_destroy)> index(
        ttrigrams_create(texts.data(), texts.size()), ttrigrams_destroy);
    require(index != nullptr, "Cannot allocate description search index");
    int limit = normalized.size() <= 3 ? 1 : 2;
    require(ttrie_search(trie.get(), normalized.c_str(), limit, collect_name_match, &found) == 0,
            "Recipe name search failed");
    require(ttrigrams_search(index.get(), normalized.c_str(), limit, collect_description_match, &descriptions) == 0,
            "Recipe description search failed");
    std::sort(found.begin(), found.end(), search_order);
    for (const auto &entry : found) {
        if (entry.tier == 6) { continue; }
        Tcl_Obj *fields[] = {Tcl_NewStringObj(entry.name.c_str(), -1), Tcl_NewStringObj(entry.description.c_str(), -1)};
        if (Tcl_ListObjAppendElement(interp, matches, Tcl_NewListObj(2, fields)) != TCL_OK) { return TCL_ERROR; }
    }
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}

extern "C" const char *ttrek_RegistryGetDirectory(void) {
    // Only explicit choices belong in project lockfiles; the default is machine-local.
    return registry_directory.empty() || registry_is_default ? nullptr : registry_argument.c_str();
}

extern "C" const char *ttrek_RegistryDefaultDirectory(void) {
    const char *home = std::getenv("HOME");
    static const std::string path = home && *home ?
        (fs::path(home) / ".local/ttrek-registry").string() : "";
    return path.empty() ? nullptr : path.c_str();
}

extern "C" int ttrek_RegistryUseDefault(Tcl_Interp *interp) try {
    if (ttrek_RegistryIsLocal()) { return TCL_OK; }
    const char *directory = ttrek_RegistryDefaultDirectory();
    if (!directory || !fs::exists(directory)) { return TCL_OK; }
    const auto stamp = fs::path(directory) / ".git/ttrek-last-sync";
    std::error_code error;
    if (!fs::is_regular_file(stamp, error)) { return TCL_OK; }
    int status = ttrek_RegistrySetDirectory(interp, directory);
    if (status == TCL_OK) { registry_is_default = true; }
    return status;
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}

extern "C" int ttrek_RegistryLoadFromLock(Tcl_Interp *interp, Tcl_Obj *project, const cJSON *lock) {
    if (ttrek_RegistryIsLocal()) { return TCL_OK; }
    auto *registry = cJSON_GetObjectItemCaseSensitive(lock, "registry");
    if (!registry) { return ttrek_RegistryUseDefault(interp); }
    if (!cJSON_IsString(registry) || !registry->valuestring[0]) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Lock file registry must be a nonempty directory string", -1));
        return TCL_ERROR;
    }
    const auto path = fs::path(Tcl_GetString(project)) / registry->valuestring;
    return ttrek_RegistrySetDirectory(interp, path.c_str());
}

extern "C" int ttrek_RegistryGetLocal(const char *url, Tcl_DString *result) try {
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
    auto response = parts.size() == 1 ? versions(parts[0]) : recipe(parts);
    std::unique_ptr<char, decltype(&cJSON_free)> json(cJSON_PrintUnformatted(response.get()), cJSON_free);
    require(json != nullptr, "Cannot serialize local registry response");
    Tcl_DStringAppend(result, json.get(), -1);
    return TCL_OK;
} catch (const std::exception &error) {
    fprintf(stderr, "error: local registry: %s\n", error.what());
    return TCL_ERROR;
}
