/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_REGISTRY_H
#define TTREK_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

int ttrek_RegistryGet(const char *url, Tcl_DString *dsPtr, cJSON *postData);

#ifdef __cplusplus
}
#endif

#endif //TTREK_REGISTRY_H