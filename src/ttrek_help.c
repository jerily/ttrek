/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "common.h"
#include "ttrek_help.h"
#include "ttrek_help_messages.h"

const char *ttrek_HelpGetTopicMessageFromObj(Tcl_Obj *topicObj) {

    int topicIdx;
    if (Tcl_GetIndexFromObjStruct(NULL, topicObj, ttrek_help_topics,
            sizeof(ttrek_help_topics[0]), NULL, 0, &topicIdx) != TCL_OK)
    {
        return NULL;
    }

    return ttrek_help_topics[topicIdx].message;

}

const char *ttrek_HelpGetTopicMessage(const char *topic) {

    Tcl_Obj *topicObj = Tcl_NewStringObj(topic, -1);

    Tcl_IncrRefCount(topicObj);
    const char *rc = ttrek_HelpGetTopicMessageFromObj(topicObj);
    Tcl_DecrRefCount(topicObj);

    return rc;

}