#include <cctype>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/utsname.h>
#include "subCmdDecls.h"
#include "registry.h"
#include "ttrek_genInstall.h"
#include "ttrek_solver.h"
#include "ttrek_useflags.h"

namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
struct TclObjectDeleter {
    void operator()(Tcl_Obj *value) const { Tcl_DecrRefCount(value); }
};
using TclObject = std::unique_ptr<Tcl_Obj, TclObjectDeleter>;

TclObject retain(Tcl_Obj *value) {
    if (value) { Tcl_IncrRefCount(value); }
    return TclObject(value);
}

struct UseFlags {
    Tcl_HashTable table;
    UseFlags() { Tcl_InitHashTable(&table, TCL_STRING_KEYS); }
    ~UseFlags() { Tcl_DeleteHashTable(&table); }
};

void require(bool condition, const std::string &message) {
    if (!condition) { throw std::runtime_error(message); }
}

void check(Tcl_Interp *interp, int status) {
    require(status == TCL_OK, Tcl_GetStringResult(interp));
}

std::string quote(const std::string &value) {
    std::string result = "'";
    for (char ch : value) { result += ch == '\'' ? "'\"'\"'" : std::string(1, ch); }
    return result + "'";
}

bool valid_name(const std::string &name) {
    return !name.empty() && name.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-") == std::string::npos;
}

Json fetch_spec(const std::string &name, const std::string &version, const utsname &platform) {
    require(valid_name(name) && valid_name(version), "Invalid package name or version");
    std::string url = std::string(REGISTRY_URL) + "/" + name + "/" + version + "/" +
        platform.sysname + "/" + platform.machine;
    Tcl_DString data;
    Tcl_DStringInit(&data);
    int status = ttrek_RegistryGet(url.c_str(), &data, nullptr);
    const std::string error = status != TCL_OK ? Tcl_DStringValue(&data) : "";
    Json spec(status == TCL_OK ? cJSON_Parse(Tcl_DStringValue(&data)) : nullptr, cJSON_Delete);
    Tcl_DStringFree(&data);
    require(status == TCL_OK && cJSON_IsObject(spec.get()),
            error.empty() ? "Cannot read build recipe for " + name + "@" + version : error);
    return spec;
}

void append_patches(std::string &script, const cJSON *spec, const std::string &name, const std::string &version) {
    const cJSON *patches = cJSON_GetObjectItemCaseSensitive(spec, "patches");
    if (!patches) { return; }
    require(cJSON_IsObject(patches), "patches must be an object");
    const cJSON *patch;
    cJSON_ArrayForEach(patch, patches) {
        require(patch->string && valid_name(patch->string) && cJSON_IsString(patch), "Invalid patch entry");
        std::string encoded = patch->valuestring;
        require(encoded.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=\r\n") ==
            std::string::npos, "Invalid base64 patch: " + std::string(patch->string));
        script += "printf '%s' " + quote(encoded) + " | base64 -d > \"$PATCH_DIR\"/" +
            quote("patch-" + name + "-" + version + "-" + patch->string) + "\n";
    }
}

void append_hook(std::string &script, const cJSON *spec, const char *name) {
    const cJSON *hook = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(spec, "scripts"), name);
    if (!hook) { return; }
    require(cJSON_IsString(hook), std::string(name) + " must be a shell script string");
    script += "\n(\ncd \"$PROJECT_HOME\"\n" + std::string(hook->valuestring) + "\n)\n";
}

void append_package(Tcl_Interp *interp, std::string &script, const std::string &name,
                    const std::string &version, cJSON *steps, ttrek_state_t &state,
                    UseFlags &flags, int current, int total) {
    auto counter = retain(ttrek_generatePackageCounter(interp, current, total));
    auto body = retain(ttrek_generateInstallScript(interp, name.c_str(), version.c_str(), nullptr,
                                                  steps, &flags.table, &state));
    require(body != nullptr, Tcl_GetStringResult(interp));
    script += "\n(\n" + std::string(Tcl_GetString(counter.get())) + Tcl_GetString(body.get()) + ")\n";
}

void validate_project(cJSON *spec, cJSON *lock) {
    require(cJSON_IsObject(spec) && cJSON_IsObject(lock), "Invalid project spec or lock JSON");
    auto *dependencies = cJSON_GetObjectItemCaseSensitive(spec, "dependencies");
    require(!dependencies || cJSON_IsObject(dependencies), "dependencies must be an object");
    const cJSON *item;
    cJSON_ArrayForEach(item, dependencies) {
        require(cJSON_IsString(item), "Project dependency constraints must be strings");
    }
    auto *packages = cJSON_GetObjectItemCaseSensitive(lock, "packages");
    require(!packages || cJSON_IsObject(packages), "Locked packages must be an object");
    cJSON_ArrayForEach(item, packages) {
        require(cJSON_IsObject(item) && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "version")),
                "Locked package must have a version string");
    }
    auto *flags = cJSON_GetObjectItemCaseSensitive(spec, "useFlags");
    require(!flags || cJSON_IsArray(flags), "useFlags must be an array");
    cJSON_ArrayForEach(item, flags) {
        require(cJSON_IsString(item) && ttrek_IsValidUseFlag(item->valuestring), "Invalid USE flag");
    }
}
} // namespace

extern "C" int ttrek_GenScriptSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    const char *shell = "bash";
    const char *strategy = "favored";
    int static_build = 0;
    Tcl_ArgvInfo options[] = {
        TTREK_REGISTRY_OPTIONS,
        {TCL_ARGV_STRING, "-shell", nullptr, &shell, "bash (default) or sh", nullptr},
        {TCL_ARGV_STRING, "-strategy", nullptr, &strategy, "latest, favored, or locked", nullptr},
        {TCL_ARGV_CONSTANT, "-static", INT2PTR(1), &static_build, "enable static linking recipes", nullptr},
        {TCL_ARGV_END, nullptr, nullptr, nullptr, nullptr, nullptr}
    };
    Tcl_Obj **remaining = nullptr;
    if (Tcl_ParseArgsObjv(interp, options, &objc, objv, &remaining) != TCL_OK) { return TCL_ERROR; }
    int status = TCL_ERROR;
    try {
        require(std::strcmp(shell, "bash") == 0 || std::strcmp(shell, "sh") == 0, "Unsupported shell: use bash or sh");
        require(std::strcmp(strategy, "favored") == 0 || std::strcmp(strategy, "latest") == 0 ||
                std::strcmp(strategy, "locked") == 0, "Invalid strategy: use latest, favored, or locked");
        for (Tcl_Size i = 1; i < objc; ++i) {
            require(Tcl_GetString(remaining[i])[0] != '-', "Unknown gen-script option: " + std::string(Tcl_GetString(remaining[i])));
        }
        TclObject project(ttrek_GetProjectHomeDir(interp, MODE_LOCAL));
        require(project != nullptr, Tcl_GetStringResult(interp));
        Json spec(ttrek_GetSpecRoot(interp, project.get()), cJSON_Delete);
        Json lock(ttrek_GetLockRoot(interp, project.get()), cJSON_Delete);
        validate_project(spec.get(), lock.get());
        check(interp, ttrek_RegistryLoadFromLock(interp, project.get(), lock.get()));
        auto flags_list = retain(Tcl_NewListObj(0, nullptr));
        check(interp, ttrek_GetUseFlags(interp, spec.get(), flags_list.get()));
        UseFlags flags;
        check(interp, ttrek_PopulateHashTableFromUseFlagsList(interp, flags_list.get(), &flags.table));
        if (static_build) {
            int is_new;
            Tcl_SetHashValue(Tcl_CreateHashEntry(&flags.table, "static", &is_new), INT2PTR(1));
            auto updated = retain(Tcl_NewListObj(0, nullptr));
            ttrek_PopulateUseFlagsListFromHashTable(interp, &flags.table, updated.get());
            Tcl_Size count;
            Tcl_Obj **values;
            Tcl_ListObjGetElements(interp, updated.get(), &count, &values);
            ttrek_SetUseFlags(interp, spec.get(), count, values);
        }
        ttrek_state_t state{};
        state.interp = interp;
        state.spec_root = spec.get();
        state.lock_root = lock.get();
        state.mode = MODE_SCRIPT;
        state.strategy = ttrek_StrategyFromString(strategy, STRATEGY_FAVORED);
        auto plan = retain(Tcl_NewListObj(0, nullptr));
        check(interp, ttrek_ResolveBuildPlan(interp, objc - 1, remaining + 1, &state, plan.get()));
        utsname platform;
        require(uname(&platform) == 0, "Cannot determine build platform");
        auto header = retain(ttrek_generateScriptHeader(shell));
        std::string script = Tcl_GetString(header.get());
        append_hook(script, spec.get(), "preInstall");
        Tcl_Size count;
        Tcl_Obj **packages;
        Tcl_ListObjGetElements(interp, plan.get(), &count, &packages);
        for (Tcl_Size i = 0; i < count; ++i) {
            Tcl_Obj *name_obj, *version_obj;
            Tcl_ListObjIndex(interp, packages[i], 0, &name_obj);
            Tcl_ListObjIndex(interp, packages[i], 1, &version_obj);
            std::string name = Tcl_GetString(name_obj), version = Tcl_GetString(version_obj);
            auto recipe = fetch_spec(name, version, platform);
            append_patches(script, recipe.get(), name, version);
            append_package(interp, script, name, version, cJSON_GetObjectItemCaseSensitive(recipe.get(), "install_script"),
                           state, flags, i + 1, count);
        }
        auto *build = cJSON_GetObjectItemCaseSensitive(spec.get(), "build");
        if (objc == 1 && build) {
            std::string platform_name = std::string(platform.sysname) + "." + platform.machine;
            for (char &ch : platform_name) { ch = std::tolower(static_cast<unsigned char>(ch)); }
            auto *steps = cJSON_GetObjectItemCaseSensitive(build, platform_name.c_str());
            if (!steps) { steps = cJSON_GetObjectItemCaseSensitive(build, "default"); }
            require(steps != nullptr, "No build recipe for " + platform_name);
            auto *name = cJSON_GetObjectItemCaseSensitive(spec.get(), "name");
            auto *version = cJSON_GetObjectItemCaseSensitive(spec.get(), "version");
            require(cJSON_IsString(name) && cJSON_IsString(version), "Project build needs name and version strings");
            require(valid_name(name->valuestring) && valid_name(version->valuestring), "Invalid project name or version");
            state.is_local_build = 1;
            append_package(interp, script, name->valuestring, version->valuestring, steps, state, flags, 1, 1);
        }
        append_hook(script, spec.get(), "postInstall");
        require(std::fwrite(script.data(), 1, script.size(), stdout) == script.size(), "Cannot write build script");
        status = TCL_OK;
    } catch (const std::exception &error) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    }
    ckfree(remaining);
    return status;
}
