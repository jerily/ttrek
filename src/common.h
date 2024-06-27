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
#else
# define DBG(x)
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

static const char *REGISTRY_URL = "http://localhost:8080/registry";
static const char *SPEC_JSON_FILE = "ttrek.json";
static const char *LOCK_JSON_FILE = "ttrek-lock.json";
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
    Tcl_Obj *project_home_dir_ptr;
    Tcl_Obj *project_venv_dir_ptr;
    Tcl_Obj *project_install_dir_ptr;
    Tcl_Obj *project_build_dir_ptr;
    Tcl_Obj *project_temp_dir_ptr;
    Tcl_Obj *spec_json_path_ptr;
    Tcl_Obj *lock_json_path_ptr;
    cJSON *spec_root;
    cJSON *lock_root;
    int option_yes;
    ttrek_mode_t mode;
    ttrek_strategy_t strategy;
} ttrek_state_t;

int ttrek_ResolvePath(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj *filename_ptr, Tcl_Obj **output_path_ptr);

int ttrek_CheckFileExists(Tcl_Obj *path_ptr);
int ttrek_EnsureSkeletonExists(Tcl_Interp *interp, ttrek_state_t *state_ptr);

int ttrek_WriteJsonFile(Tcl_Interp *interp, Tcl_Obj *path_ptr, cJSON *root);

int ttrek_ReadChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj **contents_ptr);

int ttrek_FileToJson(Tcl_Interp *interp, Tcl_Obj *path_ptr, cJSON **root);

int ttrek_WriteChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj *contents_ptr, int permissions);

int ttrek_ExecuteCommand(Tcl_Interp *interp, Tcl_Size argc, const char *argv[]);

//Tcl_Obj *ttrek_GetProjectHomeDir(Tcl_Interp *interp, ttrek_mode_t mode);
//Tcl_Obj *ttrek_GetInstallDir(Tcl_Interp *interp);
//cJSON *ttrek_GetLockRoot(Tcl_Interp *interp);
//cJSON *ttrek_GetSpecRoot(Tcl_Interp *interp);

Tcl_Obj *ttrek_GetProjectVenvDir(Tcl_Interp *interp, Tcl_Obj *project_home_dir_ptr);
ttrek_state_t *ttrek_CreateState(Tcl_Interp *interp, int option_yes, ttrek_mode_t mode, ttrek_strategy_t strategy);
void ttrek_DestroyState(ttrek_state_t *state_ptr);

ttrek_strategy_t ttrek_StrategyFromString(const char *strategy_str, ttrek_strategy_t default_strategy);
int ttrek_GetDirectDependencies(Tcl_Interp *interp, cJSON *spec_root, Tcl_Obj *list_ptr);
int ttrek_IncrRefCountObjv(Tcl_Size objc, Tcl_Obj **objv);
int ttrek_DecrRefCountObjv(Tcl_Size objc, Tcl_Obj **objv);

#ifdef __cplusplus
}
#endif

#endif //TTREK_COMMON_H
