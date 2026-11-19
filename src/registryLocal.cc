#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "registry.h"
#include "base64.h"
#include "ttrek_useflags.h"

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
            std::istringstream tokens(condition->valuestring);
            std::string flag;
            bool any = false;
            while (tokens >> flag) {
                require(ttrek_IsValidUseFlag(flag.c_str()), "Invalid USE flag in recipe 'if': " + flag);
                flags.insert(flag.substr(1));
                any = true;
            }
            require(any, "Recipe 'if' must contain a USE flag");
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
    const auto *languages = cJSON_GetObjectItemCaseSensitive(spec.get(), "languages");
    require(ttrek_LanguagesValid(languages), "Invalid recipe languages in " + directory.string());
    cJSON_AddItemToObject(result.get(), "languages", languages ? cJSON_Duplicate(languages, 1) : cJSON_CreateArray());
    cJSON_AddItemToObject(result.get(), "dependencies",
        cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(spec.get(), "dependencies"), 1));
    cJSON_AddItemToObject(result.get(), "install_script", cJSON_Duplicate(steps, 1));
    const auto *external = cJSON_GetObjectItemCaseSensitive(spec.get(), "externalDependencies");
    if (external) { cJSON_AddItemToObject(result.get(), "externalDependencies", cJSON_Duplicate(external, 1)); }
    auto scripts = resolve_scripts(cJSON_GetObjectItemCaseSensitive(spec.get(), "scripts"), directory);
    cJSON_AddItemToObject(result.get(), "scripts", scripts.release());
    auto *iuse = cJSON_AddArrayToObject(result.get(), "iuse");
    std::set<std::string> flags;
    collect_use_flags(spec.get(), flags);
    auto *declared = cJSON_GetObjectItemCaseSensitive(spec.get(), "iuse");
    require(!declared || cJSON_IsArray(declared), "iuse must be an array of unsigned USE flag names");
    const cJSON *flag;
    cJSON_ArrayForEach(flag, declared) {
        require(cJSON_IsString(flag) && ttrek_IsValidUseFlagName(flag->valuestring),
                "Invalid unsigned USE flag name in iuse");
        flags.insert(flag->valuestring);
    }
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
    require(parts.size() == 1 || parts.size() == 4 || (parts.size() == 3 && parts[2] == "languages"),
            "Invalid registry request: " + suffix);
    const fs::path registry_directory(directory);
    require(fs::is_directory(registry_directory), "Registry directory is unavailable: " + registry_directory.string());
    if (!fs::exists(fs::symlink_status(registry_directory / parts[0]))) { return TTREK_REGISTRY_NOT_FOUND; }
    Json response(nullptr, cJSON_Delete);
    if (parts.size() == 3) {
        auto spec = read_spec(registry_directory / parts[0] / parts[1]);
        const auto *languages = cJSON_GetObjectItemCaseSensitive(spec.get(), "languages");
        require(ttrek_LanguagesValid(languages), "Invalid recipe languages");
        response.reset(languages ? cJSON_Duplicate(languages, 1) : cJSON_CreateArray());
    } else {
        response = parts.size() == 1 ? versions(registry_directory, parts[0]) : recipe(registry_directory, parts);
    }
    std::unique_ptr<char, decltype(&cJSON_free)> json(cJSON_PrintUnformatted(response.get()), cJSON_free);
    require(json != nullptr, "Cannot serialize local registry response");
    Tcl_DStringAppend(result, json.get(), -1);
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_DStringSetLength(result, 0);
    Tcl_DStringAppend(result, error.what(), -1);
    return TCL_ERROR;
}

extern "C" int ttrek_LanguagesValid(const cJSON *languages) {
    if (!languages) { return 1; }
    if (!cJSON_IsArray(languages)) { return 0; }
    const cJSON *language;
    cJSON_ArrayForEach(language, languages) {
        if (!cJSON_IsString(language) || !language->valuestring[0] || strlen(language->valuestring) > 255) { return 0; }
        for (const unsigned char *ch = reinterpret_cast<const unsigned char *>(language->valuestring); *ch; ++ch) {
            if (*ch < 32 || *ch == 127) { return 0; }
        }
    }
    return 1;
}

extern "C" Tcl_Obj *ttrek_LanguagesLabel(const cJSON *languages) {
    Tcl_Obj *label = Tcl_NewObj();
    if (!ttrek_LanguagesValid(languages)) { return label; }
    std::set<std::string> names;
    const cJSON *language;
    cJSON_ArrayForEach(language, languages) { names.insert(language->valuestring); }
    const char *separator = " [";
    for (const auto &name : names) {
        Tcl_AppendToObj(label, separator, -1);
        Tcl_AppendToObj(label, name.c_str(), -1);
        separator = ", ";
    }
    if (!names.empty()) { Tcl_AppendToObj(label, "]", 1); }
    return label;
}

extern "C" cJSON *ttrek_RegistryLanguages(const char *name, const char *version) {
    // Older registries can omit this optional endpoint. Do not make display
    // metadata a prerequisite for dependency resolution or installation.
    const auto url = std::string(REGISTRY_URL) + "/" + name + "/" + version + "/languages";
    Tcl_DString data;
    Tcl_DStringInit(&data);
    int status = ttrek_RegistryGet(url.c_str(), &data, nullptr);
    cJSON *languages = status == TCL_OK ? cJSON_ParseWithLengthOpts(
        Tcl_DStringValue(&data), Tcl_DStringLength(&data) + 1, nullptr, 1) : nullptr;
    Tcl_DStringFree(&data);
    if (!cJSON_IsArray(languages) || !ttrek_LanguagesValid(languages)) {
        cJSON_Delete(languages);
        return nullptr;
    }
    return languages;
}
