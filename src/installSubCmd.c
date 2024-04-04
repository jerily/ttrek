#include <string.h>
#include <curl/curl.h>
#include "subCmdDecls.h"
#include "common.h"
#include "base64.h"

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

    char install_filename[256];
    snprintf(install_filename, sizeof(install_filename), "build/install-%s-%s.sh", name, version);
    char install_spec_url[256];
    snprintf(install_spec_url, sizeof(install_spec_url), "%s/%s/%s", REGISTRY_URL, name, version);

    struct MemoryStruct chunk;

    chunk.memory = Tcl_Alloc(1);  /* grown as needed by the realloc above */
    chunk.size = 0;    /* no data at this point */

    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, install_spec_url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_cb);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ttrek/1.0");

    CURLcode ret = curl_easy_perform(curl_handle);
    if (ret == CURLE_OK) {
        fprintf(stderr, "%lu bytes retrieved\n", (unsigned long)chunk.size);
    } else {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(ret));
        SetResult("failed to fetch spec file");
        return TCL_ERROR;
    }
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    cJSON *install_spec_root = cJSON_Parse(chunk.memory);
    cJSON *install_script_node = cJSON_GetObjectItem(install_spec_root, "install_script");
    if (!install_script_node) {
        fprintf(stderr, "error: install_script not found in spec file\n");
        cJSON_free(install_spec_root);
        return TCL_ERROR;
    }
    Tcl_Free(chunk.memory);

    const char *base64_install_script_str = install_script_node->valuestring;

    fprintf(stderr, "install_script: %s\n", base64_install_script_str);

    cJSON *patches = cJSON_GetObjectItem(install_spec_root, "patches");
    if (patches) {
        for (int i = 0; i < cJSON_GetArraySize(patches); i++) {
            cJSON *patch_item = cJSON_GetArrayItem(patches, i);
            const char *patch_name = patch_item->string;
            const char *base64_patch_diff = patch_item->valuestring;
            fprintf(stderr, "patch_name: %s\n", patch_name);
//            fprintf(stderr, "patch_diff: %s\n", base64_patch_diff);

            char patch_diff[1024*1024];
            Tcl_Size patch_diff_len;
            base64_decode(base64_patch_diff, strnlen(base64_patch_diff, 1024*1024), patch_diff, &patch_diff_len);

            char patch_filename[256];
            snprintf(patch_filename, sizeof(install_filename), "build/%s", patch_name);

            Tcl_Obj *patch_file_path_ptr;
            ttrek_ResolvePath(interp, path_to_rootdir, Tcl_NewStringObj(patch_filename, -1), &patch_file_path_ptr);
            ttrek_WriteChars(interp, patch_file_path_ptr, Tcl_NewStringObj(patch_diff, -1), 0666);

        }
    }


    const size_t MAX_INSTALL_SCRIPT_LEN = 1024*1024;
    char install_script[MAX_INSTALL_SCRIPT_LEN];
    Tcl_Size install_script_len;
    base64_decode(base64_install_script_str, strnlen(base64_install_script_str, MAX_INSTALL_SCRIPT_LEN), install_script, &install_script_len);

    Tcl_Obj *install_file_path_ptr;
    ttrek_ResolvePath(interp, path_to_rootdir, Tcl_NewStringObj(install_filename, -1), &install_file_path_ptr);
    ttrek_WriteChars(interp, install_file_path_ptr, Tcl_NewStringObj(install_script, install_script_len), 0777);

    int deps_length = 0;
    Tcl_Obj *deps_list_ptr = Tcl_NewListObj(0, NULL);
    cJSON *dependencies = cJSON_GetObjectItem(install_spec_root, "dependencies");
    for (int i = 0; i < cJSON_GetArraySize(dependencies); i++) {
        cJSON *dep_item = cJSON_GetArrayItem(dependencies, i);
        const char *dep_name = dep_item->string;
        const char *dep_version = dep_item->valuestring;
        fprintf(stderr, "dep_name: %s\n", dep_name);
        fprintf(stderr, "dep_version: %s\n", dep_version);
        // add to list of dependencies
        Tcl_Obj *objv[2] = {Tcl_NewStringObj(dep_name, -1), Tcl_NewStringObj(dep_version, -1)};
        Tcl_Obj *dep_list_ptr = Tcl_NewListObj(2, objv);
        Tcl_ListObjAppendElement(interp, deps_list_ptr, dep_list_ptr);
        deps_length++;
    }
    cJSON_free(install_spec_root);


    for (int i = 0; i < deps_length; i++) {
        Tcl_Obj *dep_list_ptr;
        Tcl_ListObjIndex(interp, deps_list_ptr, i, &dep_list_ptr);
        Tcl_Obj *dep_name_ptr;
        Tcl_Obj *dep_version_ptr;
        Tcl_ListObjIndex(interp, dep_list_ptr, 0, &dep_name_ptr);
        Tcl_ListObjIndex(interp, dep_list_ptr, 1, &dep_version_ptr);
        const char *dep_name = Tcl_GetString(dep_name_ptr);
        const char *dep_version = Tcl_GetString(dep_version_ptr);
        if (TCL_OK !=
            ttrek_InstallDependency(interp, path_to_rootdir, NULL, path_to_lock_file_ptr, dep_name, dep_version)) {
            fprintf(stderr, "error: could not install dependency: %s@%s\n", dep_name, dep_version);
            return TCL_ERROR;
        }
    }

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

int ttrek_InstallSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
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
