/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "ttrek_help.h"

static const struct {
    const char *command;
    const char *message;
} ttrek_help_topics[] = {
    {"general",
#include "help_general.txt.h"
    },
    {"install",
#include "help_install.txt.h"
    },
    {"uninstall",
#include "help_uninstall.txt.h"
    },
    {"init",
#include "help_init.txt.h"
    },
    {"run",
#include "help_run.txt.h"
    },
    {"update",
#include "help_update.txt.h"
    },
    {"ls",
#include "help_ls.txt.h"
    },
    {NULL, NULL}
};

