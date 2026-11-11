#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/utsname.h>
#include <unistd.h>
#include "PackageDatabase.h"
#include "subCmdDecls.h"

namespace {
namespace fs = std::filesystem;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

void require(bool condition, const std::string &message) {
    if (!condition) { throw std::runtime_error(message); }
}

// Keep Tcl arguments alive across the existing download/unpack helpers.
int command(Tcl_Interp *interp, int (*proc)(Tcl_Interp *, Tcl_Size, Tcl_Obj *const *),
            const std::vector<std::string> &arguments) {
    std::vector<Tcl_Obj *> objects;
    for (const auto &argument : arguments) {
        auto *object = Tcl_NewStringObj(argument.c_str(), -1);
        Tcl_IncrRefCount(object);
        objects.push_back(object);
    }
    int status = proc(interp, objects.size(), objects.data());
    for (auto *object : objects) { Tcl_DecrRefCount(object); }
    return status;
}

Json recipe(const std::string &name, const std::string &version) {
    struct utsname platform;
    require(uname(&platform) == 0, "Cannot determine the source platform");
    const auto url = std::string(REGISTRY_URL) + "/" + name + "/" + version + "/" +
                     platform.sysname + "/" + platform.machine;
    Tcl_DString data;
    Tcl_DStringInit(&data);
    int status = ttrek_RegistryGet(url.c_str(), &data, nullptr);
    std::string body = Tcl_DStringValue(&data);
    Tcl_DStringFree(&data);
    require(status == TCL_OK, body.empty() ? "Could not read source recipe" : body);
    Json result(cJSON_ParseWithLengthOpts(body.c_str(), body.size() + 1, nullptr, 1), cJSON_Delete);
    require(cJSON_IsObject(result.get()), "Invalid source recipe");
    return result;
}

const cJSON *source_download(const cJSON *recipe) {
    auto *steps = cJSON_GetObjectItemCaseSensitive(recipe, "install_script");
    require(cJSON_IsArray(steps), "Source recipe must contain an install_script array");
    std::map<std::string, const cJSON *> downloads;
    const cJSON *step;
    cJSON_ArrayForEach(step, steps) {
        auto *cmd = cJSON_GetObjectItemCaseSensitive(step, "cmd");
        require(cJSON_IsString(cmd), "Invalid source recipe command");
        std::string kind = cmd->valuestring;
        if (kind != "download" && kind != "unpack") { continue; }
        auto *filename = cJSON_GetObjectItemCaseSensitive(step, "filename");
        require(!filename || (cJSON_IsString(filename) && filename->valuestring[0]),
                "Source archive filename must be a nonempty string");
        const std::string key = filename ? filename->valuestring : "";
        if (kind == "download") {
            downloads[key] = step;
        } else if (!cJSON_HasObjectItem(step, "destination")) {
            auto download = downloads.find(key);
            require(download != downloads.end(), "Source unpack step has no matching download");
            require(!cJSON_HasObjectItem(step, "if") && !cJSON_HasObjectItem(download->second, "if"),
                    "Cannot clone a conditional source archive without USE flag selection");
            auto *url = cJSON_GetObjectItemCaseSensitive(download->second, "url");
            require(cJSON_IsString(url) && url->valuestring[0], "Source download URL must be a nonempty string");
            return download->second;
        }
    }
    throw std::runtime_error("Package has no source archive to download and unpack");
}

void check_destination(const fs::path &path) {
    require(!fs::is_symlink(fs::symlink_status(path)) &&
            (!fs::exists(path) || (fs::is_directory(path) && fs::is_empty(path))),
            "Clone destination must be a new or empty directory: " + path.string());
}

struct StagingDirectory {
    fs::path path;
    ~StagingDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};
}

extern "C" int ttrek_CloneSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) try {
    const char *outdir = nullptr;
    Tcl_ArgvInfo options[] = {
        {TCL_ARGV_STRING, "-outdir", nullptr, &outdir, "source destination (default: package name)", nullptr},
        TTREK_REGISTRY_OPTIONS,
        {TCL_ARGV_END, nullptr, nullptr, nullptr, nullptr, nullptr}
    };
    Tcl_Obj **remaining = nullptr;
    if (Tcl_ParseArgsObjv(interp, options, &objc, objv, &remaining) != TCL_OK) { return TCL_ERROR; }
    std::unique_ptr<Tcl_Obj *, decltype(&Tcl_Free)> arguments(remaining, Tcl_Free);
    require(objc == 2, "Usage: ttrek clone [-outdir DIRECTORY] PACKAGE[@VERSION]");
    std::string argument = Tcl_GetString(remaining[1]);
    auto separator = argument.find('@');
    const auto name = argument.substr(0, separator);
    const auto constraint = separator == std::string::npos ? "" : argument.substr(separator + 1);
    require(!name.empty() && name.size() <= 128 &&
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-") == std::string::npos &&
            std::isalnum(static_cast<unsigned char>(name.front())), "Invalid package name");
    require(!outdir || *outdir, "-outdir must not be empty");
    auto destination = fs::absolute(outdir ? outdir : name).lexically_normal();
    while (destination.filename().empty() && destination != destination.root_path()) {
        destination = destination.parent_path();
    }
    // Preserve a caller's final symlink check before canonicalizing its parent.
    check_destination(destination);
    const auto range = version_range(constraint.empty() ? std::nullopt : std::optional<std::string_view>(constraint));
    if (ttrek_RegistrySelectForRead(interp) != TCL_OK) { return TCL_ERROR; }
    std::optional<Pack> selected;
    std::string version;
    for (const auto &[candidate, dependencies] : fetch_package_versions(name)) {
        Pack parsed(candidate);
        if (!range.segments.empty() && range.contains(parsed) && (!selected || parsed > *selected)) {
            selected = parsed;
            version = candidate;
        }
    }
    require(selected.has_value(), "No matching version for " + argument);
    require(name.size() + version.size() + 1 <= 200 &&
            version.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ._+-") == std::string::npos,
            "Invalid source version identifier");
    auto spec = recipe(name, version);
    const auto *download = source_download(spec.get());
    const auto *url = cJSON_GetObjectItemCaseSensitive(download, "url");
    const auto *filename = cJSON_GetObjectItemCaseSensitive(download, "filename");

    fs::create_directories(destination.parent_path());
    destination = fs::canonical(destination.parent_path()) / destination.filename();
    std::string pattern = (destination.parent_path() / ".ttrek-clone-XXXXXX").string();
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    require(mkdtemp(temporary.data()) != nullptr, "Cannot create temporary clone directory");
    StagingDirectory staging{temporary.data()};
    const auto archive = staging.path / "source.archive";
    const auto source = staging.path / "source";
    fs::create_directory(source);
    std::printf("Downloading %s@%s...\n", name.c_str(), version.c_str());
    std::fflush(stdout);
    std::vector<std::string> download_args{"download", url->valuestring, archive.string(), name + "-" + version};
    if (filename) { download_args.push_back(filename->valuestring); }
    if (command(interp, ttrek_DownloadSubCmd, download_args) != TCL_OK) { return TCL_ERROR; }
    if (command(interp, ttrek_UnpackSubCmd, {"unpack", archive.string(), source.string()}) != TCL_OK) { return TCL_ERROR; }
    require(!fs::is_empty(source), "Source archive is empty");
    check_destination(destination);
    if (fs::exists(destination)) {
        // Keep an existing directory's inode, including when -outdir is '.'.
        std::vector<fs::path> moved;
        try {
            for (const auto &entry : fs::directory_iterator(source)) {
                auto target = destination / entry.path().filename();
                fs::rename(entry.path(), target);
                moved.push_back(target);
            }
        } catch (...) {
            for (const auto &path : moved) {
                std::error_code error;
                fs::remove_all(path, error);
            }
            throw;
        }
    } else {
        fs::rename(source, destination);
    }
    std::printf("Cloned %s@%s into %s\n", name.c_str(), version.c_str(), destination.c_str());
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}
