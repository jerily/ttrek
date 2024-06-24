# Copyright Jerily LTD. All Rights Reserved.
# SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
# SPDX-License-Identifier: MIT.

namespace eval ::ttrek::helper {}

namespace eval ::ttrek::helper {

    variable banner {
        ttrek v@@VERSION@@ - The package manager for TCL
        Copyright Jerily LTD. All Rights Reserved.
    }

    array set sections {

        common {
            Usage: @@CMD@@ COMMAND [options]

            Available commands:
                init
                install
                uninstall
                run
                pretend

            Run '@@CMD@@ COMMAND help' for more information on a command.
        }

        install {
            Usage: @@CMD@@ install [options] ?package_A@version_range_A? ?package_B@version_range_B? ?...?

            Available options:
                -u - install using user mode (~/.local)
                -g - install using global mode (/usr/local/ttrek)
                default - If no mode is specified, install using local mode (./ttrek-venv)

            package is the package name e.g. twebserver

            version_range is a comma-separated list of operator (op) and version pairs where op is:

            ^  - caret operator accepts all versions that have the same major version
            >= - greater than or equal operator ...
            >  - greater than operator ...
            <= - less than or equal operator ...
            <  - less than operator ...
            == - equals operator ...

            Examples:
                twebserver@^1.47.37
                "twebserver@>=1.47.37,<1.50.0"
        }

        unknown {
            There is no specific help on the specified command.
        }

    }


}

proc ::ttrek::helper::unindent { text } {
    set text [string trim $text \n]
    set text [split $text \n]
    set line [lindex $text 0]
    set padnum [expr { [string length $line] - [string length [string trimleft $line]] }]
    set result [list]
    foreach line $text {
        lappend result [string range $line $padnum end]
    }
    return [string trim [join $result \n] \n]
}

proc ::ttrek::helper::print { args } {

    set chan "stdout"
    set newline 1

    foreach arg $args {
        switch -exact -- $arg {
            -stderr    { set chan "stderr" }
            -stdout    { set chan "stdout" }
            -nonewline { set newline 0 }
            default {
                # If the argument is unknown, assume that this message is
                break
            }
        }
    }

    set text [unindent $arg]
    set text [string map [list \
        "@@VERSION@@" [package present ttrek] \
        "@@CMD@@"     [file tail [info nameofexecutable]] \
    ] $text]
    puts $chan $text
    if { $newline } {
        puts $chan ""
    }

}

proc ::ttrek::helper::run { {section ""} } {

    variable banner
    variable sections

    if { $section eq "" } {
        set section "common"
    } elseif { ![info exists sections($section)] } {
        set section "unknown"
    }

    print $banner
    print -nonewline $sections($section)
    exit 0

}

proc ::ttrek::helper::usage { msg {section common} } {

    variable banner
    variable sections

    if { $section eq "" } {
        set section "common"
    } elseif { ![info exists sections($section)] } {
        set section "unknown"
    }

    print $banner
    print $msg
    print -stderr -nonewline $sections($section)
    exit 1

}

package provide ttrek::helper 1.0.0
