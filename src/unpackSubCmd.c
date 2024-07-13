/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include <archive.h>
#include <archive_entry.h>

static int ttrek_UnpackCopyData(Tcl_Interp *interp, struct archive *ar,
    struct archive *aw)
{

  int r;
  const void *buff;
  size_t size;
  la_int64_t offset;

  for (;;) {

      r = archive_read_data_block(ar, &buff, &size, &offset);

      if (r == ARCHIVE_EOF) {
          break;
      }

      if (r < ARCHIVE_OK) {
          if (r < ARCHIVE_WARN) {
              Tcl_SetObjResult(interp, Tcl_ObjPrintf("unable to read a block"
                  ": %s", archive_error_string(ar)));
              return TCL_ERROR;
          } else {
              fprintf(stderr, "WARNING: %s\n", archive_error_string(ar));
          }
      }

      r = archive_write_data_block(aw, buff, size, offset);

      if (r < ARCHIVE_OK) {
          if (r < ARCHIVE_WARN) {
              Tcl_SetObjResult(interp, Tcl_ObjPrintf("unable to write a block"
                  ": %s", archive_error_string(aw)));
              return TCL_ERROR;
          } else {
              fprintf(stderr, "WARNING: %s\n", archive_error_string(aw));
          }
      }

  }

  return TCL_OK;

}

// This function does the following:
//     1. strips the first path elements from entryPath
//     2. joins bashPath + stipped entryPath
//     3. returns the joined path as Tcl_Obj with refcount=0
Tcl_Obj *ttrek_UnpackGetOutputName(Tcl_Obj *basePath, const char *entryPath) {

    // DBG2(printf("join [%s] with [%s]", Tcl_GetString(basePath), entryPath));

    Tcl_Obj *entryPathObj = Tcl_NewStringObj(entryPath, -1);
    Tcl_IncrRefCount(entryPathObj);

    Tcl_Size countElements;
    Tcl_Obj *entrySplitObj = Tcl_FSSplitPath(entryPathObj, &countElements);
    // DBG2(printf("got list [%s] with %" TCL_SIZE_MODIFIER "d elements",
    //     Tcl_GetString(entrySplitObj), countElements));
    Tcl_IncrRefCount(entrySplitObj);

    Tcl_DecrRefCount(entryPathObj);

    // Remove the 1st path element
    if (countElements > 1) {
        Tcl_ListObjReplace(NULL, entrySplitObj, 0, 1, 0, NULL);
    }

    // Insert the base path as the 1st element
    Tcl_ListObjReplace(NULL, entrySplitObj, 0, 0, 1, &basePath);

    // DBG2(printf("join elements from the list [%s]", Tcl_GetString(entrySplitObj)));
    Tcl_Obj *rc = Tcl_FSJoinPath(entrySplitObj, -1);

    Tcl_DecrRefCount(entrySplitObj);

    return rc;

}

int ttrek_UnpackSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    if (objc != 3) {
        SetResult("not enough arguments");
        return TCL_ERROR;
    }

    struct archive *a = NULL;
    struct archive *ext = NULL;
    struct archive_entry *entry;
    int r;
    int flags;
    Tcl_Obj *outputFileName = NULL;

    flags |= ARCHIVE_EXTRACT_PERM;
    flags |= ARCHIVE_EXTRACT_SECURE_NODOTDOT;
    flags |= ARCHIVE_EXTRACT_SECURE_SYMLINKS;

    a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    r = archive_read_open_filename(a, Tcl_GetString(objv[1]), 10240);

    if (r != ARCHIVE_OK) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unable to open archive \"%s\":"
            " %s", Tcl_GetString(objv[1]), archive_error_string(a)));
        goto error;
    }

    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    for (;;) {

        r = archive_read_next_header(a, &entry);

        if (r == ARCHIVE_EOF) {
            break;
        }

        if (r < ARCHIVE_OK) {
            if (r < ARCHIVE_WARN) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unable to read the"
                    " archive \"%s\": %s", Tcl_GetString(objv[1]),
                    archive_error_string(a)));
                goto error;
            } else {
                fprintf(stderr, "WARNING READER: %s\n", archive_error_string(a));
            }
        }

        const char *entryPath = archive_entry_pathname(entry);

        outputFileName = ttrek_UnpackGetOutputName(objv[2], entryPath);
        Tcl_IncrRefCount(outputFileName);

        printf("Extracting entry: '%s' -> '%s'\n",
            archive_entry_pathname(entry), Tcl_GetString(outputFileName));

        archive_entry_set_pathname(entry, Tcl_GetString(outputFileName));

        Tcl_DecrRefCount(outputFileName);
        outputFileName = NULL;

        r = archive_write_header(ext, entry);

        if (r < ARCHIVE_OK) {
            fprintf(stderr, "WARNING WRITER: %s\n", archive_error_string(ext));
        } else if (archive_entry_size(entry) > 0) {
            if (ttrek_UnpackCopyData(interp, a, ext) != TCL_OK) {
                goto error;
            }
        }

        r = archive_write_finish_entry(ext);

        if (r < ARCHIVE_OK) {
            if (r < ARCHIVE_WARN) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unable to write file:"
                    " %s", archive_error_string(ext)));
                goto error;
            } else {
                fprintf(stderr, "WARNING WRITER: %s\n", archive_error_string(ext));
            }
        }


    }

    r = archive_read_free(a);
    if (r != ARCHIVE_OK) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("error while closing archive"
            " \"%s\": %d", Tcl_GetString(objv[1]), r));
        return TCL_ERROR;
    }

    archive_write_free(ext);

    return TCL_OK;

error:
    if (a != NULL) {
        archive_read_free(a);
    }
    if (ext != NULL) {
        archive_write_free(ext);
    }
    if (outputFileName != NULL) {
        Tcl_DecrRefCount(outputFileName);
    }
    return TCL_ERROR;

}
