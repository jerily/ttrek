/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_COMMON_H
#define TTREK_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <tcl.h>
#include "cjson/cJSON.h"

#ifndef TCL_SIZE_MAX
typedef int Tcl_Size;
# define Tcl_GetSizeIntFromObj Tcl_GetIntFromObj
# define Tcl_NewSizeIntObj Tcl_NewIntObj
# define TCL_SIZE_MAX      INT_MAX
# define TCL_SIZE_MODIFIER ""
#endif

#define XSTR(s) STR(s)
#define STR(s) #s

#ifdef DEBUG
# define DBG(x) x
#ifndef __FUNCTION_NAME__
    #ifdef _WIN32   // WINDOWS
        #define __FUNCTION_NAME__   __FUNCTION__
    #else          // GCC
        #define __FUNCTION_NAME__   __func__
    #endif
#endif
# define DBG2(x) {printf("%s: ", __FUNCTION_NAME__); x; printf("\n"); fflush(stdout);}
#else
# define DBG(x)
# define DBG2(x)
#endif

#define SetResult(str) Tcl_ResetResult(interp); \
                     Tcl_SetStringObj(Tcl_GetObjResult(interp), (str), -1)

#define SubCmdProc(x) int (x)(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])

/*
 * Macros used to cast between pointers and integers (e.g. when storing an int
 * in ClientData), on 64-bit architectures they avoid gcc warning about "cast
 * to/from pointer from/to integer of different size".
 */

#if !defined(INT2PTR) && !defined(PTR2INT)
#   if defined(HAVE_INTPTR_T) || defined(intptr_t)
#	define INT2PTR(p) ((void *)(intptr_t)(p))
#	define PTR2INT(p) ((int)(intptr_t)(p))
#   else
#	define INT2PTR(p) ((void *)(p))
#	define PTR2INT(p) ((int)(p))
#   endif
#endif


static const char *VERSION = XSTR(PROJECT_VERSION);


#define TTREK_REGISTRY_BASE_URL "http://localhost:8080"

static const char *REGISTRY_URL = TTREK_REGISTRY_BASE_URL "/registry";
static const char *TELEMETRY_REGISTER_URL = TTREK_REGISTRY_BASE_URL "/telemetry/register";
static const char *TELEMETRY_COLLECT_URL = TTREK_REGISTRY_BASE_URL "/telemetry/collect";
static const char *SPEC_JSON_FILE = "ttrek.json";
static const char *LOCK_JSON_FILE = "ttrek-lock.json";
static const char *MANIFEST_JSON_FILE = "ttrek-manifest.json";
static const char *DIRTY_FILE = ".dirty";
static const char *LOCKING_FILE = ".lock";
static const char *LOCKING_FILE_IGNORE_RULE = "/.lock";
static const char *BUILD_DIR_IGNORE_RULE = "/build";
static const char *VENV_DIR = "ttrek-venv";
static const char *INSTALL_DIR = "local";
static const char *BUILD_DIR = "build";
static const char *TEMP_DIR = "tmp";

typedef enum {
    MODE_LOCAL,
    MODE_USER,
    MODE_GLOBAL
} ttrek_mode_t;

typedef enum {
    STRATEGY_LATEST,
    STRATEGY_FAVORED,
    STRATEGY_LOCKED
} ttrek_strategy_t;

typedef struct {
    Tcl_Interp *interp;
    Tcl_Obj *project_home_dir_ptr;
    Tcl_Obj *project_venv_dir_ptr;
    Tcl_Obj *project_install_dir_ptr;
    Tcl_Obj *project_build_dir_ptr;
    Tcl_Obj *project_temp_dir_ptr;
    Tcl_Obj *spec_json_path_ptr;
    Tcl_Obj *lock_json_path_ptr;
    Tcl_Obj *manifest_json_path_ptr;
    Tcl_Obj *dirty_file_path_ptr;
    Tcl_Obj *locking_file_path_ptr;
    cJSON *spec_root;
    cJSON *lock_root;
    cJSON *manifest_root;
    int with_locking;
    int option_yes;
    int option_force;
    ttrek_mode_t mode;
    ttrek_strategy_t strategy;
    int lock_fd;
} ttrek_state_t;

int ttrek_ResolvePath(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj *filename_ptr, Tcl_Obj **output_path_ptr);

Tcl_Obj *ttrek_GetHomeDirectory();

int ttrek_CheckFileExists(Tcl_Obj *path_ptr);
int ttrek_FileExists(Tcl_Interp *interp, Tcl_Obj *path_ptr, int *exists);
int ttrek_DirectoryExists(Tcl_Interp *interp, Tcl_Obj *dir_path_ptr, int *result_exists);
int ttrek_EnsureDirectoryExists(Tcl_Interp *interp, Tcl_Obj *dir_path_ptr);
int ttrek_EnsureSkeletonExists(Tcl_Interp *interp, ttrek_state_t *state_ptr);
Tcl_Obj *ttrek_GetVenvSubDir(Tcl_Interp *interp, Tcl_Obj *project_venv_dir_ptr, const char *subdir);

int ttrek_WriteJsonFile(Tcl_Interp *interp, Tcl_Obj *path_ptr, cJSON *root);

int ttrek_ReadChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj **contents_ptr);

int ttrek_FileToJson(Tcl_Interp *interp, Tcl_Obj *path_ptr, cJSON **root);

int ttrek_WriteChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj *contents_ptr, int permissions);

int ttrek_TouchFile(Tcl_Interp *interp, Tcl_Obj *path_ptr);

int ttrek_ExecuteCommand(Tcl_Interp *interp, Tcl_Size argc, const char *argv[], Tcl_Obj *resultObj);

Tcl_Obj *ttrek_GetProjectVenvDir(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr);
ttrek_state_t *ttrek_CreateState(Tcl_Interp *interp, int option_yes, int option_force, int with_locking, ttrek_mode_t mode, ttrek_strategy_t strategy);
void ttrek_DestroyState(ttrek_state_t *state_ptr);

ttrek_strategy_t ttrek_StrategyFromString(const char *strategy_str, ttrek_strategy_t default_strategy);
int ttrek_GetDirectDependencies(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *list_ptr);

int ttrek_InitSpecFile(Tcl_Interp *interp, Tcl_Obj *path_to_spec_ptr, const char *project_name, const char *project_version);
int ttrek_InitLockFile(Tcl_Interp *interp, Tcl_Obj *path_to_lock_ptr);

#ifdef __cplusplus
}
#endif

#endif //TTREK_COMMON_H