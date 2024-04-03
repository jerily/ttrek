/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_COMMON_H
#define TTREK_COMMON_H

#include "cjson/cJSON.h"

#ifndef TCL_SIZE_MAX
typedef int Tcl_Size;
# define Tcl_GetSizeIntFromObj Tcl_GetIntFromObj
# define Tcl_NewSizeIntObj Tcl_NewIntObj
# define TCL_SIZE_MAX      INT_MAX
# define TCL_SIZE_MODIFIER ""
#endif

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

int ttrek_ResolvePath(Tcl_Interp *interp, Tcl_Obj *current_working_directory, Tcl_Obj *filename_ptr, Tcl_Obj **path_ptr);
int ttrek_CheckFileExists(Tcl_Obj *path_ptr);
int ttrek_WriteJsonFile(Tcl_Interp *interp, Tcl_Obj *path_ptr, cJSON *root);
int ttrek_ReadChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj **contents_ptr);
int ttrek_FileToJson(Tcl_Interp *interp, Tcl_Obj *path_ptr, cJSON **root);
int ttrek_WriteChars(Tcl_Interp *interp, Tcl_Obj *path_ptr, Tcl_Obj *contents_ptr, int permissions);

#endif //TTREK_COMMON_H
