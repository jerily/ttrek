# Copyright Jerily LTD. All Rights Reserved.
# SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
# SPDX-License-Identifier: MIT.

package ifneeded ttrek::helper 1.0.0 [list source [file join $dir ttrek_helper.tcl]]

package ifneeded ttrek 1.0.0 [list source [file join $dir ttrek.tcl]]
