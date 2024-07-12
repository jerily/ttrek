/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_HELP_H
#define TTREK_HELP_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *ttrek_HelpGetTopicMessage(const char *topic);
const char *ttrek_HelpGetTopicMessageFromObj(Tcl_Obj *topicObj);

#ifdef __cplusplus
}
#endif

#endif //TTREK_HELP_H