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

// Test if we can also select a nested version
void test_unit_propagation_nested() {
    auto provider = BundleBoxProvider::from_packages({{{"asdf"}, 1, std::vector<std::string>{"efgh"}},
                                                      {{"efgh"}, 4, std::vector<std::string>()},
                                                      {{"dummy"}, 6, std::vector<std::string>()}});
    auto root_requirements = provider.requirements({"asdf"});
    auto pool_ptr = provider.pool;
    auto solver = Solver<Range<Pack>, std::string, BundleBoxProvider>(provider);
    auto [solved, err] = solver.solve(root_requirements);

    assert(!err.has_value());
    assert(solved.size() == 2);

    auto solvable = pool_ptr->resolve_solvable(solved[0]);
    assert(pool_ptr->resolve_package_name(solvable.get_name_id()) == "asdf");
    assert(solvable.get_inner().version == 1);

    solvable = pool_ptr->resolve_solvable(solved[1]);
    assert(pool_ptr->resolve_package_name(solvable.get_name_id()) == "efgh");
    assert(solvable.get_inner().version == 4);
    fprintf(stdout, "success\n");
}

// Test if we can resolve multiple versions at once
void test_resolve_multiple() {
    auto provider = BundleBoxProvider::from_packages({{{"asdf"}, 1, std::vector<std::string>()},
                                                      {{"asdf"}, 2, std::vector<std::string>()},
                                                      {{"efgh"}, 4, std::vector<std::string>()},
                                                      {{"efgh"}, 5, std::vector<std::string>()}});
    auto root_requirements = provider.requirements({"asdf", "efgh"});
    auto pool_ptr = provider.pool;
    auto solver = Solver<Range<Pack>, std::string, BundleBoxProvider>(provider);
    auto [solved, err] = solver.solve(root_requirements);

    assert(!err.has_value());
    assert(solved.size() == 2);

    auto solvable = pool_ptr->resolve_solvable(solved[0]);
    assert(pool_ptr->resolve_package_name(solvable.get_name_id()) == "asdf");
    assert(solvable.get_inner().version == 2);

    solvable = pool_ptr->resolve_solvable(solved[1]);
    assert(pool_ptr->resolve_package_name(solvable.get_name_id()) == "efgh");
    assert(solvable.get_inner().version == 5);
    fprintf(stdout, "success\n");
}

#define assert_snapshot(snapshot) assert(check_equal_to_snapshot(__func__, snapshot))

template <typename S>
bool check_equal_to_snapshot(const char* func, const S& snapshot) {
    fprintf(stderr, ">>>>>>>>>>>>>>>>>>>>> check_equal_to_snapshot: func=%s\n", func);
    fprintf(stderr, "snapshot=%s\n", snapshot.c_str());
    // TODO: read snapshot file and check if it matches the given snapshot
    return true;
}

void test_resolve_with_concurrent_metadata_fetching() {
    auto provider = BundleBoxProvider::from_packages({{{"parent"}, 4, std::vector<std::string>{"child1", "child2"}},
                                                      {{"child1"}, 3, std::vector<std::string>()},
                                                      {{"child2"}, 2, std::vector<std::string>()}});
//    auto max_concurrent_requests = provider.concurrent_requests_max;
    auto result = solve_snapshot(provider, {"parent"});
    assert_snapshot(result);

//    assert(max_concurrent_requests.get() == 2);
    fprintf(stdout, "success\n");
}

// In case of a conflict the version should not be selected with the conflict
void test_resolve_with_conflict() {
    auto provider = BundleBoxProvider::from_packages({{{"asdf"}, 4, std::vector<std::string>{"conflicting 1"}},
                                                      {{"asdf"}, 3, std::vector<std::string>{"conflicting 0"}},
                                                      {{"efgh"}, 7, std::vector<std::string>{"conflicting 0"}},
                                                      {{"efgh"}, 6, std::vector<std::string>{"conflicting 0"}},
                                                      {{"conflicting"}, 1, std::vector<std::string>()},
                                                      {{"conflicting"}, 0, std::vector<std::string>()}});
    auto result = solve_snapshot(provider, {"asdf", "efgh"});
    assert_snapshot(result);

}
int ttrek_PretendSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

//    test_unit_propagation_1();
//    test_unit_propagation_nested();
//    test_resolve_multiple();
//    test_resolve_with_concurrent_metadata_fetching();
    test_resolve_with_conflict();

    return TCL_OK;
}

