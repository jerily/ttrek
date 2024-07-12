/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_SUBCMDDECLS_H
#define TTREK_SUBCMDDECLS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

SubCmdProc(ttrek_InitSubCmd);
SubCmdProc(ttrek_InstallSubCmd);
SubCmdProc(ttrek_RunSubCmd);
SubCmdProc(ttrek_UpdateSubCmd);
SubCmdProc(ttrek_ListSubCmd);
SubCmdProc(ttrek_UninstallSubCmd);
SubCmdProc(ttrek_DownloadSubCmd);
SubCmdProc(ttrek_UnpackSubCmd);
SubCmdProc(ttrek_HelpSubCmd);

#ifdef __cplusplus
}
#endif

#endif //TTREK_SUBCMDDECLS_H