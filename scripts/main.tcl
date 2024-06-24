# Copyright Jerily LTD. All Rights Reserved.
# SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
# SPDX-License-Identifier: MIT.

package require ttrek

set command [lindex $argv 0]

if { $command in {help ""} } {
    ttrek::helper::run [lindex $argv 1]
}

if { [lindex $argv 1] eq "help" } {
    ttrek::helper::run $command
}

# process other commands here

ttrek::helper::usage "Error: Unknown command \"$command\" was specified."
