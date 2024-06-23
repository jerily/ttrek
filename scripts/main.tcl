# Copyright Jerily LTD. All Rights Reserved.
# SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
# SPDX-License-Identifier: MIT.

package require ttrek

set help_message "
ttrek v[package present ttrek] - The package manager for TCL
Copyright Jerily LTD. All Rights Reserved.
"

set help_message_common "$help_message

Usage: [info nameofexecutable] sub-command \[options\]

Available sub-commands:
    install - install package
    uninstall - uninstall package
"

set help_message_install "$help_message

Usage: [info nameofexecutable] install \[options\]

Available options:
   -foo - install foo
   -bla - install bla
"

set command [lindex $argv 0]

switch -exact -- $command {
    help {
        set msg $help_message_common
        switch -- [lindex $argv 1] {
            install {
                set msg $help_message_install
            }
        }
        puts $msg
        exit 0
    }
    default {
        puts stderr "Unknown command: \"$command\""
        puts stderr $help_message_common
        exit 1
    }
}
