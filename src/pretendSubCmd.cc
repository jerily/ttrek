/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include "subCmdDecls.h"
#include "resolvo/tests/solver.h"
#include "resolvo/internal/tracing.h"

void test_unit_propagation_1() {
    // test_unit_propagation_1
    auto provider = BundleBoxProvider::from_packages({{{"asdf"}, 1, std::vector<std::string>()}});
    auto root_requirements = provider.requirements({"asdf"});
    auto pool_ptr = provider.pool;
    auto solver = Solver<Range<Pack>, std::string, BundleBoxProvider>(provider);
    auto [solved, err] = solver.solve(root_requirements);

    assert(!err.has_value());

    auto solvable = pool_ptr->resolve_solvable(solved[0]);
    assert(pool_ptr->resolve_package_name(solvable.get_name_id()) == "asdf");
    assert(solvable.get_inner().version == 1);
    fprintf(stdout, "success\n");
}

int ttrek_PretendSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

    test_unit_propagation_1();

    return TCL_OK;
}

