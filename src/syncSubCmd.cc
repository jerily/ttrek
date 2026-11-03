#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "subCmdDecls.h"
#include "registry.h"

namespace {
namespace fs = std::filesystem;
const char *registry_url = "https://github.com/jerily/ttrek-registry.git";

std::string git(Tcl_Interp *interp, std::initializer_list<std::string> arguments) {
    std::vector<const char *> argv{"git"};
    for (const auto &argument : arguments) { argv.push_back(argument.c_str()); }
    argv.push_back("2>@1");
    Tcl_Obj *output = Tcl_NewObj();
    Tcl_IncrRefCount(output);
    int status = ttrek_ExecuteCommand(interp, argv.size(), argv.data(), output);
    std::string text = Tcl_GetString(output);
    Tcl_DecrRefCount(output);
    if (status != TCL_OK) { throw std::runtime_error("git failed: " + text + Tcl_GetStringResult(interp)); }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) { text.pop_back(); }
    return text;
}
}

extern "C" int ttrek_SyncSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) try {
    if (objc > 2) { throw std::runtime_error("Usage: ttrek sync [directory]"); }
    const char *argument = objc == 2 ? Tcl_GetString(objv[1]) : ttrek_RegistryDefaultDirectory();
    if (!argument || !*argument) { throw std::runtime_error("Set HOME or specify a registry directory"); }
    if (argument[0] == '-') { throw std::runtime_error("Usage: ttrek sync [directory]"); }
    const auto directory = fs::absolute(argument).lexically_normal();
    const auto path = directory.string();
    if (fs::exists(directory / ".git")) {
        if (git(interp, {"-C", path, "config", "--get", "remote.origin.url"}) != registry_url) {
            throw std::runtime_error("Destination is not a jerily/ttrek-registry checkout");
        }
        if (git(interp, {"-C", path, "symbolic-ref", "--short", "HEAD"}) != "main") {
            throw std::runtime_error("Registry checkout must be on main before syncing");
        }
        if (!git(interp, {"-C", path, "status", "--porcelain"}).empty()) {
            throw std::runtime_error("Registry checkout has local changes; commit or remove them before syncing");
        }
        git(interp, {"-C", path, "pull", "--ff-only", "origin", "main"});
    } else {
        if (fs::exists(directory) && (!fs::is_directory(directory) || !fs::is_empty(directory))) {
            throw std::runtime_error("Registry destination must be empty or an existing registry checkout");
        }
        fs::create_directories(directory.parent_path());
        git(interp, {"clone", "--branch", "main", "--single-branch", registry_url, path});
    }
    // Git resolves the metadata directory correctly for both clones and worktrees.
    auto git_directory = fs::path(git(interp, {"-C", path, "rev-parse", "--absolute-git-dir"}));
    std::ofstream stamp(git_directory / "ttrek-last-sync");
    if (!stamp) { throw std::runtime_error("Registry synced, but could not record its sync time"); }
    stamp << git(interp, {"-C", path, "rev-parse", "HEAD"}) << '\n';
    std::printf("Registry synced to %s\n", path.c_str());
    return TCL_OK;
} catch (const std::exception &error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
}
