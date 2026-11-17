#include <cctype>
#include <stdexcept>
#include <string>
#include "recipeDependencies.h"
#include "registry.h"
#include "ttrek_genInstall.h"
#include "ttrek_useflags.h"

namespace {
void require(bool condition, const std::string &message) {
    if (!condition) { throw std::runtime_error(message); }
}

std::string quote(const std::string &value) {
    std::string result = "'";
    for (char ch : value) { result += ch == '\'' ? "'\"'\"'" : std::string(1, ch); }
    return result + "'";
}

std::string report(const std::string &message) {
    return "printf '%s\\n' " + quote(message) + " >&2\n";
}

bool nonempty_string(const cJSON *value) {
    return cJSON_IsString(value) && value->valuestring[0];
}

// uname capitalizes OS names differently (Darwin, Linux, FreeBSD). Match the
// documented lowercase platform names without needing another host command.
std::string platform_pattern(const char *platform) {
    std::string pattern;
    for (const unsigned char ch : std::string(platform)) {
        if (ch >= 'a' && ch <= 'z') {
            pattern += "[" + std::string(1, ch) + std::string(1, std::toupper(ch)) + "]";
        } else {
            require((ch >= '0' && ch <= '9') || ch == '_' || ch == '-',
                    "platforms must contain lowercase operating system names");
            pattern += ch;
        }
    }
    return pattern;
}
}

extern "C" cJSON *ttrek_FetchRecipe(Tcl_Interp *interp, const char *name, const char *version,
                                    const char *os, const char *arch) {
    const std::string url = std::string(REGISTRY_URL) + "/" + name + "/" + version + "/" + os + "/" + arch;
    Tcl_DString data;
    Tcl_DStringInit(&data);
    int status = ttrek_RegistryGet(url.c_str(), &data, nullptr);
    cJSON *recipe = status == TCL_OK ? cJSON_Parse(Tcl_DStringValue(&data)) : nullptr;
    if (!cJSON_IsObject(recipe)) {
        Tcl_SetObjResult(interp, status != TCL_OK && Tcl_DStringLength(&data)
            ? Tcl_NewStringObj(Tcl_DStringValue(&data), Tcl_DStringLength(&data))
            : Tcl_ObjPrintf("Could not read build recipe for %s@%s", name, version));
        cJSON_Delete(recipe);
        recipe = nullptr;
    }
    Tcl_DStringFree(&data);
    if (recipe && !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(recipe, "install_script"))) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s@%s: build instructions must be an array", name, version));
        cJSON_Delete(recipe);
        recipe = nullptr;
    }
    if (recipe) {
        cJSON *iuse = cJSON_GetObjectItemCaseSensitive(recipe, "iuse");
        if (!iuse) { iuse = cJSON_AddArrayToObject(recipe, "iuse"); }
        Tcl_Obj *flags = Tcl_NewListObj(0, nullptr);
        Tcl_IncrRefCount(flags);
        status = ttrek_PopulateIUseFlagsListFromNode(interp, iuse, flags);
        Tcl_DecrRefCount(flags);
        if (status != TCL_OK) { cJSON_Delete(recipe); recipe = nullptr; }
    }
    return recipe;
}

extern "C" int ttrek_AppendRecipeChecks(Tcl_Interp *interp, Tcl_Obj *script, const char *name,
    const char *version, const cJSON *external, const cJSON *steps, Tcl_HashTable *flags) {
    const std::string label = std::string(name) + "@" + version;
    try {
        std::string checks;
        require(!external || cJSON_IsObject(external), "externalDependencies must be an object");
        const cJSON *dependency;
        cJSON_ArrayForEach(dependency, external) {
            require(dependency->string && dependency->string[0] && cJSON_IsObject(dependency),
                    "externalDependencies entries must be named objects");
            const std::string context = "externalDependencies." + std::string(dependency->string);
            const auto *platforms = cJSON_GetObjectItemCaseSensitive(dependency, "platforms");
            const auto *check = cJSON_GetObjectItemCaseSensitive(dependency, "check");
            const auto *files = cJSON_GetObjectItemCaseSensitive(check, "files");
            const auto *hint = cJSON_GetObjectItemCaseSensitive(dependency, "installHint");
            const auto *url = cJSON_GetObjectItemCaseSensitive(dependency, "url");
            require(!platforms || cJSON_IsArray(platforms), context + ".platforms must be an array");
            require(cJSON_IsObject(check) && cJSON_IsArray(files) && cJSON_GetArraySize(files) > 0,
                    context + ".check.files must be a nonempty array of paths");
            require(!hint || cJSON_IsString(hint), context + ".installHint must be a string");
            require(!url || cJSON_IsString(url), context + ".url must be a string");
            const cJSON *item;
            cJSON_ArrayForEach(item, check) {
                require(std::string(item->string) == "files", context + ".check contains an unsupported check");
            }
            std::string patterns;
            cJSON_ArrayForEach(item, platforms) {
                require(nonempty_string(item), context + ".platforms entries must be nonempty strings");
                if (!patterns.empty()) { patterns += "|"; }
                patterns += platform_pattern(item->valuestring);
            }
            std::string body = "_ttrek_dependency_missing=0\n";
            cJSON_ArrayForEach(item, files) {
                require(nonempty_string(item), context + ".check.files entries must be nonempty strings");
                body += "if [ ! -f " + quote(item->valuestring) + " ]; then\n" +
                    report("ttrek: " + label + " requires " + dependency->string +
                           "; missing file: " + item->valuestring) +
                    "_ttrek_dependency_missing=1\n_ttrek_missing=1\nfi\n";
            }
            body += "if [ \"$_ttrek_dependency_missing\" = 1 ]; then\n";
            body += report("Install the external dependency before retrying.");
            if (hint && hint->valuestring[0]) { body += report("  " + std::string(hint->valuestring)); }
            if (url && url->valuestring[0]) { body += report("  " + std::string(url->valuestring)); }
            body += "fi\n";
            if (!platforms) { checks += body; }
            else if (!patterns.empty()) { checks += "case \"$(uname -s)\" in\n" + patterns + ")\n" + body + ";;\nesac\n"; }
        }

        bool cmake = false, autoconf = false;
        require(!steps || cJSON_IsArray(steps), "build instructions must be an array");
        const cJSON *step;
        cJSON_ArrayForEach(step, steps) {
            int enabled;
            if (ttrek_IsUseFlagEnabled(interp, flags, step, &enabled) != TCL_OK) { return TCL_ERROR; }
            if (!enabled) { continue; }
            const auto *cmd = cJSON_GetObjectItemCaseSensitive(step, "cmd");
            require(nonempty_string(cmd), "build instruction needs a cmd string");
            const std::string command = cmd->valuestring;
            cmake |= command == "cmake_config" || command == "cmake_make" || command == "cmake_install";
            const auto *path = cJSON_GetObjectItemCaseSensitive(step, "path");
            autoconf |= command == "autogen" && cJSON_IsString(path) && std::string(path->valuestring) == "autoconf";
        }
        for (const auto *tool : {"cmake", "autoconf"}) {
            if ((std::string(tool) == "cmake" && !cmake) || (std::string(tool) == "autoconf" && !autoconf)) { continue; }
            checks += "if ! command -v " + quote(tool) + " >/dev/null 2>&1; then\n" +
                report("ttrek: " + label + " requires " + tool + ", but it was not found on PATH.") +
                report("  macOS: brew install " + std::string(tool)) +
                report("  Ubuntu/Debian: sudo apt install " + std::string(tool)) +
                "_ttrek_missing=1\nfi\n";
        }
        Tcl_AppendToObj(script, checks.c_str(), checks.size());
        return TCL_OK;
    } catch (const std::exception &error) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s: %s", label.c_str(), error.what()));
        return TCL_ERROR;
    }
}

extern "C" int ttrek_RunRecipeChecks(Tcl_Interp *interp, Tcl_Obj *script) {
    Tcl_AppendToObj(script, "[ \"$_ttrek_missing\" = 0 ]\n", -1);
    const char *argv[] = {"/bin/sh", "-c", Tcl_GetString(script), nullptr};
    if (ttrek_ExecuteCommand(interp, 3, argv, nullptr) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("external dependency check failed", -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}

extern "C" int ttrek_CheckProjectDependencies(Tcl_Interp *interp, ttrek_state_t *state) {
    Tcl_Obj *flags_list = Tcl_NewListObj(0, nullptr);
    Tcl_IncrRefCount(flags_list);
    Tcl_HashTable flags;
    Tcl_InitHashTable(&flags, TCL_STRING_KEYS);
    int status = ttrek_GetUseFlags(interp, state->manifest_root, flags_list);
    if (status == TCL_OK) { status = ttrek_PopulateHashTableFromUseFlagsList(interp, flags_list, &flags); }
    Tcl_DecrRefCount(flags_list);
    Tcl_Obj *checks = Tcl_NewStringObj("_ttrek_missing=0\n", -1);
    Tcl_IncrRefCount(checks);
    const auto *name = cJSON_GetObjectItem(state->spec_root, "name");
    const auto *version = cJSON_GetObjectItem(state->spec_root, "version");
    if (status == TCL_OK) {
        status = ttrek_AppendRecipeChecks(interp, checks,
            cJSON_IsString(name) ? name->valuestring : "project",
            cJSON_IsString(version) ? version->valuestring : "unknown",
            cJSON_GetObjectItemCaseSensitive(state->spec_root, "externalDependencies"),
            cJSON_GetObjectItem(cJSON_GetObjectItem(state->spec_root, "build"), "default"), &flags);
    }
    if (status == TCL_OK) { status = ttrek_RunRecipeChecks(interp, checks); }
    Tcl_DecrRefCount(checks);
    Tcl_DeleteHashTable(&flags);
    return status;
}
