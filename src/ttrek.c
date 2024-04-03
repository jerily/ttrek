/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <tcl.h>
#include <string.h>
#include <cjson/cJSON.h>
#include "common.h"
#include <curl/curl.h>

#define XSTR(s) STR(s)
#define STR(s) #s

#ifdef DEBUG
# define DBG(x) x
#else
# define DBG(x)
#endif

static const char *VERSION = XSTR(PROJECT_VERSION);

static const char *REGISTRY_URL = "http://localhost:8080/registry";
static const char *PACKAGES_JSON_FILE = "ttrek.json";
static const char *LOCK_JSON_FILE = "ttrek-lock.json";

static int ttrek_InitSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    // write to file
    Tcl_Obj *filename_ptr = Tcl_NewStringObj(PACKAGES_JSON_FILE, -1);
    Tcl_IncrRefCount(filename_ptr);
    Tcl_Obj *cwd = Tcl_FSGetCwd(interp);
    if (!cwd) {
        fprintf(stderr, "error: getting current working directory failed\n");
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(cwd);
    Tcl_Obj *path_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, cwd, filename_ptr, &path_ptr)) {
        Tcl_DecrRefCount(cwd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(cwd);

    Tcl_IncrRefCount(path_ptr);
    if (TCL_OK == ttrek_CheckFileExists(path_ptr)) {
        fprintf(stderr, "error: %s already exists\n", PACKAGES_JSON_FILE);
        return TCL_ERROR;
    }


    int option_yes = 0;
    int option_force = 0;
    Tcl_ArgvInfo ArgTable[] = {
            {TCL_ARGV_CONSTANT, "-y", INT2PTR(1), &option_yes, "Automatically answer yes to all the questions"},
            {TCL_ARGV_CONSTANT, "-f", INT2PTR(1), &option_force, "Removes various protections against unfortunate side effects."},
            {TCL_ARGV_END,      NULL, NULL,               NULL,             NULL}
    };

    Tcl_Obj **remObjv;
    Tcl_ParseArgsObjv(interp, ArgTable, &objc, objv, &remObjv);

//    fprintf(stderr, "option_yes: %d\n", option_yes);
//    fprintf(stderr, "option_force: %d\n", option_force);

    char project_name[256] = "example";
    char project_version[256] = "1.0.0";
    if (!option_yes) {
        // read "name" and "version" from stdin
        fprintf(stdout, "name: ");
        fgets(project_name, sizeof(project_name), stdin);
        fprintf(stdout, "version: ");
        fgets(project_version, sizeof(project_version), stdin);

        // remove trailing newline
        project_name[strcspn(project_name, "\n")] = 0;
        project_version[strcspn(project_version, "\n")] = 0;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *name = cJSON_CreateString(project_name);
    cJSON_AddItemToObject(root, "name", name);
    cJSON *version = cJSON_CreateString(project_version);
    cJSON_AddItemToObject(root, "version", version);
    cJSON *scripts = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "scripts", scripts);
    cJSON *dependencies = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "dependencies", dependencies);
    cJSON *devDependencies = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "devDependencies", devDependencies);

    ttrek_WriteJsonFile(interp, path_ptr, root);

    ckfree(remObjv);
    return TCL_OK;
}

static int ttrek_AddPackageToJsonFile(Tcl_Interp *interp, Tcl_Obj *path_ptr, const char *name, const char *version) {
    cJSON *root = NULL;
    if (TCL_OK != ttrek_FileToJson(interp, path_ptr, &root)) {
        fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_ptr));
        return TCL_ERROR;
    }

    cJSON *dependencies = cJSON_GetObjectItem(root, "dependencies");
    cJSON *pkg = cJSON_GetObjectItem(dependencies, name);
    if (pkg) {
        // modify the value
        cJSON_ReplaceItemInObject(dependencies, name, cJSON_CreateString(version));
    } else {
        cJSON_AddItemToObject(dependencies, name, cJSON_CreateString(version));
    }
    cJSON *devDependencies = cJSON_GetObjectItem(root, "devDependencies");
    cJSON_free(root);

    ttrek_WriteJsonFile(interp, path_ptr, root);
    return TCL_OK;
}

static int ttrek_EnsureLockFileExists(Tcl_Interp *interp, Tcl_Obj *path_ptr) {
    if (TCL_OK != ttrek_CheckFileExists(path_ptr)) {
        cJSON *root = cJSON_CreateObject();
        cJSON *packages = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "packages", packages);
        return ttrek_WriteJsonFile(interp, path_ptr, root);
    }
    return TCL_OK;
}

static int ttrek_AddPackageToLockFile(Tcl_Interp *interp, Tcl_Obj *path_ptr, const char *name, const char *version) {

    if (TCL_OK != ttrek_EnsureLockFileExists(interp, path_ptr)) {
        fprintf(stderr, "error: could not create %s\n", Tcl_GetString(path_ptr));
        return TCL_ERROR;
    }

    cJSON *root = NULL;
    if (TCL_OK != ttrek_FileToJson(interp, path_ptr, &root)) {
        fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_ptr));
        return TCL_ERROR;
    }

    cJSON *packages = cJSON_GetObjectItem(root, "packages");
    cJSON *pkg = cJSON_GetObjectItem(packages, name);
    if (pkg) {
        // modify the value
        cJSON_ReplaceItemInObject(packages, name, cJSON_CreateString(version));
    } else {
        cJSON_AddItemToObject(packages, name, cJSON_CreateString(version));
    }
    cJSON_free(root);

    ttrek_WriteJsonFile(interp, path_ptr, root);
    return TCL_OK;
}

static int ttrek_GetPackageVersionFromLockFile(Tcl_Interp *interp, Tcl_Obj *path_ptr, const char *name, Tcl_Obj **installed_version) {
    if (TCL_OK != ttrek_EnsureLockFileExists(interp, path_ptr)) {
        fprintf(stderr, "error: could not create %s\n", Tcl_GetString(path_ptr));
        return TCL_ERROR;
    }

    cJSON *root = NULL;
    if (TCL_OK != ttrek_FileToJson(interp, path_ptr, &root)) {
        fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_ptr));
        return TCL_ERROR;
    }

    cJSON *packages = cJSON_GetObjectItem(root, "packages");
    cJSON *pkg = cJSON_GetObjectItem(packages, name);
    if (pkg) {
        *installed_version = Tcl_NewStringObj(pkg->valuestring, -1);
        Tcl_IncrRefCount(*installed_version);
    } else {
        *installed_version = NULL;
    }
    cJSON_free(root);
    return TCL_OK;
}

struct MemoryStruct {
    char *memory;
    Tcl_Size size;
};

static size_t write_memory_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = Tcl_Realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

static int ttrek_InstallDependency(Tcl_Interp *interp, Tcl_Obj *path_to_rootdir, Tcl_Obj *path_to_packages_file_ptr, Tcl_Obj *path_to_lock_file_ptr, const char *name, const char *version) {
    Tcl_Obj *installed_version = NULL;
    if (TCL_OK != ttrek_GetPackageVersionFromLockFile(interp, path_to_lock_file_ptr, name, &installed_version)) {
        fprintf(stderr, "error: could not get version for %s\n", name);
        return TCL_ERROR;
    }
    if (installed_version) {
        if (strcmp(Tcl_GetString(installed_version), version) == 0) {
            Tcl_DecrRefCount(installed_version);
            fprintf(stderr, "info: %s@%s is already installed\n", name, version);
            return TCL_OK;
        }
        Tcl_DecrRefCount(installed_version);
    }

    char spec_filename[256];
    snprintf(spec_filename, sizeof(spec_filename), "registry/%s/%s/ttrek.json", name, version);
    Tcl_Obj *path_to_spec_file_ptr;
    ttrek_ResolvePath(interp, path_to_rootdir, Tcl_NewStringObj(spec_filename, -1), &path_to_spec_file_ptr);

    cJSON *root = NULL;
    if (TCL_OK != ttrek_FileToJson(interp, path_to_spec_file_ptr, &root)) {
        fprintf(stderr, "error: could not read %s\n", Tcl_GetString(path_to_spec_file_ptr));
        return TCL_ERROR;
    }

    cJSON *dependencies = cJSON_GetObjectItem(root, "dependencies");
    for (int i = 0; i < cJSON_GetArraySize(dependencies); i++) {
        cJSON *dep_item = cJSON_GetArrayItem(dependencies, i);
        const char *dep_name = dep_item->string;
        const char *dep_version = dep_item->valuestring;
        fprintf(stderr, "dep_name: %s\n", dep_name);
        fprintf(stderr, "dep_version: %s\n", dep_version);
        if (TCL_OK != ttrek_InstallDependency(interp, path_to_rootdir, NULL, path_to_lock_file_ptr, dep_name, dep_version)) {
            fprintf(stderr, "error: could not install dependency: %s@%s\n", dep_name, dep_version);
            return TCL_ERROR;
        }
    }
    cJSON_free(root);


    char install_filename[256];
    snprintf(install_filename, sizeof(install_filename), "build/install-%s-%s.sh", name, version);
    char install_script_url[256];
    snprintf(install_script_url, sizeof(spec_filename), "%s/%s/%s/install.sh", REGISTRY_URL, name, version);

    struct MemoryStruct chunk;

    chunk.memory = Tcl_Alloc(1);  /* grown as needed by the realloc above */
    chunk.size = 0;    /* no data at this point */

    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, install_script_url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_cb);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ttrek/1.0");

    CURLcode ret = curl_easy_perform(curl_handle);
    if (ret == CURLE_OK) {
        fprintf(stderr, "%lu bytes retrieved\n", (unsigned long)chunk.size);
    } else {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(ret));
    }
    curl_easy_cleanup(curl_handle);

    // write "chunk.memory" to file "install_filename"
    Tcl_Obj *install_file_path_ptr;
    ttrek_ResolvePath(interp, path_to_rootdir, Tcl_NewStringObj(install_filename, -1), &install_file_path_ptr);
    ttrek_WriteChars(interp, install_file_path_ptr, Tcl_NewStringObj(chunk.memory, chunk.size), 0777);

    Tcl_Free(chunk.memory);
    curl_global_cleanup();

    Tcl_Obj *path_to_install_file_ptr;
    ttrek_ResolvePath(interp, path_to_rootdir, Tcl_NewStringObj(install_filename, -1), &path_to_install_file_ptr);

    int argc = 2;
    const char *argv[3] = {Tcl_GetString(path_to_install_file_ptr), Tcl_GetString(path_to_rootdir), NULL };
    fprintf(stderr, "path_to_install_file: %s\n", Tcl_GetString(path_to_install_file_ptr));
    Tcl_Channel chan = Tcl_OpenCommandChannel(interp, argc, argv, TCL_STDOUT|TCL_STDERR);
    Tcl_Obj *resultPtr = Tcl_NewStringObj("", -1);
    if (Tcl_GetChannelHandle(chan, TCL_READABLE, NULL) == TCL_OK) {
        if (Tcl_ReadChars(chan, resultPtr, -1, 0) < 0) {
            fprintf(stderr, "error reading from channel: %s\n", Tcl_GetString(Tcl_GetObjResult(interp)));
            return TCL_ERROR;
        }
        fprintf(stderr, "result: %s\n", Tcl_GetString(resultPtr));
    }
    Tcl_Close(interp, chan);
    fprintf(stderr, "interp result: %s\n", Tcl_GetString(Tcl_GetObjResult(interp)));

    if (path_to_packages_file_ptr) {
        ttrek_AddPackageToJsonFile(interp, path_to_packages_file_ptr, name, version);
    }
    ttrek_AddPackageToLockFile(interp, path_to_lock_file_ptr, name, version);

    return TCL_OK;
}

static int ttrek_InstallSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *cwd = Tcl_FSGetCwd(interp);
    if (!cwd) {
        fprintf(stderr, "error: getting current working directory failed\n");
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(cwd);

    Tcl_Obj *packages_filename_ptr = Tcl_NewStringObj(PACKAGES_JSON_FILE, -1);
    Tcl_IncrRefCount(packages_filename_ptr);
    Tcl_Obj *path_to_packages_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, cwd, packages_filename_ptr, &path_to_packages_file_ptr)) {
        Tcl_DecrRefCount(packages_filename_ptr);
        Tcl_DecrRefCount(cwd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(packages_filename_ptr);
    Tcl_IncrRefCount(path_to_packages_file_ptr);

    Tcl_Obj *lock_filename_ptr = Tcl_NewStringObj(LOCK_JSON_FILE, -1);
    Tcl_IncrRefCount(lock_filename_ptr);
    Tcl_Obj *path_to_lock_file_ptr;
    if (TCL_OK != ttrek_ResolvePath(interp, cwd, lock_filename_ptr, &path_to_lock_file_ptr)) {
        Tcl_DecrRefCount(lock_filename_ptr);
        Tcl_DecrRefCount(cwd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(lock_filename_ptr);
    Tcl_IncrRefCount(path_to_lock_file_ptr);

    if (TCL_OK != ttrek_CheckFileExists(path_to_packages_file_ptr)) {
        fprintf(stderr, "error: %s does not exist, run 'ttrek init' first\n", PACKAGES_JSON_FILE);
        Tcl_DecrRefCount(cwd);
        Tcl_DecrRefCount(path_to_packages_file_ptr);
        return TCL_ERROR;
    }

    int option_save_dev = 0;
    int option_global = 0;
    Tcl_ArgvInfo ArgTable[] = {
            {TCL_ARGV_CONSTANT, "--save-dev", INT2PTR(1), &option_save_dev, "Save the package to the local repository as a dev dependency"},
            {TCL_ARGV_CONSTANT, "--global", INT2PTR(1), &option_global, "install as a global package"},
            {TCL_ARGV_END, NULL, NULL, NULL, NULL}
    };

    Tcl_Obj **remObjv;
    Tcl_ParseArgsObjv(interp, ArgTable, &objc, objv, &remObjv);

    Tcl_Size package_name_length;
    const char *package = Tcl_GetStringFromObj(remObjv[1], &package_name_length);
    // "package" is of the form "name@version"
    // we need to split it into "name" and "version"
    const char *package_name = strtok(package, "@");
    const char *package_version = strtok(NULL, "@");

    fprintf(stderr, "package_name: %s\n", package_name);
    fprintf(stderr, "package_version: %s\n", package_version);

    fprintf(stderr, "option_save_dev: %d\n", option_save_dev);
    fprintf(stderr, "objc: %d remObjv: %s\n", objc, Tcl_GetString(remObjv[0]));

    Tcl_Obj *homeDirPtr = Tcl_GetVar2Ex(interp, "env", "HOME", TCL_GLOBAL_ONLY);
    fprintf(stderr, "homeDirPtr: %s\n", Tcl_GetString(homeDirPtr));

    if (TCL_OK != ttrek_InstallDependency(interp, cwd, path_to_packages_file_ptr, path_to_lock_file_ptr, package_name, package_version)) {
        fprintf(stderr, "error: could not install dependency: %s@%s\n", package_name, package_version);
        Tcl_DecrRefCount(cwd);
        Tcl_DecrRefCount(path_to_packages_file_ptr);
        ckfree(remObjv);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(cwd);
    Tcl_DecrRefCount(path_to_packages_file_ptr);
    ckfree(remObjv);
    return TCL_OK;
}

static const char *subcommands[] = {
        "init",
        "install",
        "uninstall",
        NULL
};

enum subcommand {
    SUBCMD_INIT,
    SUBCMD_INSTALL,
    SUBCMD_UNINSTALL
};

int main(int argc, char *argv[]) {

    if (argc <= 1) {
        fprintf(stderr, "Usage: ttrek <subcommand> [options]\n");
        return 1;
    }

    Tcl_Interp *interp = Tcl_CreateInterp();
    Tcl_Size objc = argc;
    Tcl_Obj **objv = (Tcl_Obj **) Tcl_Alloc(sizeof(Tcl_Obj *) * argc);
    for (int i = 0; i < argc; i++) {
        objv[i] = Tcl_NewStringObj(argv[i], -1);
    }

    int sub_cmd_index;
    if (TCL_OK == Tcl_GetIndexFromObj(interp, objv[1], subcommands, "subcommand", 0, &sub_cmd_index)) {
        switch ((enum subcommand) sub_cmd_index) {
            case SUBCMD_INIT:
                fprintf(stderr, "init\n");
                ttrek_InitSubCmd(interp, objc-1, &objv[1]);
                break;
            case SUBCMD_INSTALL:
                fprintf(stderr, "install\n");
                ttrek_InstallSubCmd(interp, objc-1, &objv[1]);
                break;
            case SUBCMD_UNINSTALL:
                fprintf(stderr, "uninstall\n");
                break;
        }
    }

    return 0;
}