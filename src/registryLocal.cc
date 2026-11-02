#include <algorithm>
#include <cctype>
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

namespace {
namespace fs = std::filesystem;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
fs::path registry_directory;
std::string registry_argument;

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
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}

extern "C" int ttrek_RegistryIsLocal(void) {
    return !registry_directory.empty();
}

extern "C" const char *ttrek_RegistryGetDirectory(void) {
    return registry_directory.empty() ? nullptr : registry_argument.c_str();
}

extern "C" int ttrek_RegistryLoadFromLock(Tcl_Interp *interp, Tcl_Obj *project, const cJSON *lock) {
    if (ttrek_RegistryIsLocal()) { return TCL_OK; }
    auto *registry = cJSON_GetObjectItemCaseSensitive(lock, "registry");
    if (!registry) { return TCL_OK; }
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
