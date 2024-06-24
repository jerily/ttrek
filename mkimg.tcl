# Copyright Jerily LTD. All Rights Reserved.
# SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
# SPDX-License-Identifier: MIT.

set source_dir [file dirname [file normalize [info script]]]
set script_dir [file join $source_dir scripts]

for { set i 0 } { $i < $argc } { incr i } {
    set arg [lindex $argv $i]
    switch -exact -- $arg {
        -o {
            set output_file [lindex $argv [incr i]]
        }
        default {
            puts stderr "Unknown argument: \"$arg\""
            exit 1
        }
    }
}

if { ![info exists output_file] } {
    puts stderr "No output file specified"
    exit 1
}

if { [file exists ttrek.vfs] } {
    file delete -force ttrek.vfs
}
if { [file exists $output_file] } {
    file delete -force $output_file
}

file mkdir ttrek.vfs
file copy [file join [zipfs root] app tcl_library] ttrek.vfs
file copy [file join $script_dir main.tcl] ttrek.vfs
file copy [file join $script_dir tcl_library ttrek] ttrek.vfs/tcl_library

zipfs mkzip $output_file ttrek.vfs ttrek.vfs
file attributes $output_file -permissions 0o0644

puts "Created: $output_file"