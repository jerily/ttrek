/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#include <utility>

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

void test_literal_satisfying_value() {
    auto lit = Literal {
        SolvableId::root(),
        true,
    };
    assert(lit.satisfying_value() == false);

    lit = Literal {
        SolvableId::root(),
        false,
    };
    assert(lit.satisfying_value() == true);
}

void test_literal_eval() {
    auto decision_map = DecisionMap();
    auto lit = Literal {
        SolvableId::root(),
        false,
    };
    auto negated_lit = Literal {
        SolvableId::root(),
        true,
    };

    // Undecided
    assert(lit.eval(decision_map) == std::nullopt);
    assert(negated_lit.eval(decision_map) == std::nullopt);

    // Decided
    decision_map.set(SolvableId::root(), true, 1);
    assert(lit.eval(decision_map) == true);
    assert(negated_lit.eval(decision_map) == false);

    decision_map.set(SolvableId::root(), false, 1);
    assert(lit.eval(decision_map) == false);
    assert(negated_lit.eval(decision_map) == true);
}

ClauseState clause(std::array<ClauseId, 2> next_clauses, std::array<SolvableId, 2> watched_solvables) {
    return ClauseState(
        std::move(watched_solvables),
        std::move(next_clauses),

        // the kind is irrelevant here
        Clause::InstallRoot{}
    );
}

void test_unlink_clause_different() {
    auto clause1 = clause(
        {ClauseId::from_usize(2), ClauseId::from_usize(3)},
        {SolvableId::from_usize(1596), SolvableId::from_usize(1211)}
    );
    auto clause2 = clause(
        {ClauseId::null(), ClauseId::from_usize(3)},
        {SolvableId::from_usize(1596), SolvableId::from_usize(1208)}
    );
    auto clause3 = clause(
        {ClauseId::null(), ClauseId::null()},
        {SolvableId::from_usize(1211), SolvableId::from_usize(42)}
    );

    // Unlink 0
    {
        auto clause1_copy = clause1;
        clause1_copy.unlink_clause(clause2, SolvableId::from_usize(1596), 0);
        auto vec1 = std::vector<SolvableId>{SolvableId::from_usize(1596), SolvableId::from_usize(1211)};
        auto vec2 = std::vector<ClauseId>{ClauseId::null(), ClauseId::from_usize(3)};
        assert(std::vector(clause1_copy.watched_literals_.begin(), clause1_copy.watched_literals_.end()) == vec1);
        assert(std::vector(clause1_copy.next_watches_.begin(), clause1_copy.next_watches_.end()) == vec2);
    }

    // Unlink 1
    {
        auto clause1_copy = clause1;
        clause1_copy.unlink_clause(clause3, SolvableId::from_usize(1211), 0);
        auto vec1 = std::vector<SolvableId>{SolvableId::from_usize(1596), SolvableId::from_usize(1211)};
        auto vec2 = std::vector<ClauseId>{ClauseId::from_usize(2), ClauseId::null()};
        assert(std::vector(clause1_copy.watched_literals_.begin(), clause1_copy.watched_literals_.end()) == vec1);
        assert(std::vector(clause1_copy.next_watches_.begin(), clause1_copy.next_watches_.end()) == vec2);
    }
}

void test_unlink_clause_same() {
    auto clause1 = clause(
        {ClauseId::from_usize(2), ClauseId::from_usize(2)},
        {SolvableId::from_usize(1596), SolvableId::from_usize(1211)}
    );
    auto clause2 = clause(
        {ClauseId::null(), ClauseId::null()},
        {SolvableId::from_usize(1596), SolvableId::from_usize(1211)}
    );

    // Unlink 0
    {
        auto clause1_copy = clause1;
        clause1_copy.unlink_clause(clause2, SolvableId::from_usize(1596), 0);
        auto vec1 = std::vector<SolvableId>{SolvableId::from_usize(1596), SolvableId::from_usize(1211)};
        auto vec2 = std::vector<ClauseId>{ClauseId::null(), ClauseId::from_usize(2)};
        assert(std::vector(clause1_copy.watched_literals_.begin(), clause1_copy.watched_literals_.end()) == vec1);
        assert(std::vector(clause1_copy.next_watches_.begin(), clause1_copy.next_watches_.end()) == vec2);
    }

    // Unlink 1
    {
        auto clause1_copy = clause1;
        clause1_copy.unlink_clause(clause2, SolvableId::from_usize(1211), 1);
        auto vec1 = std::vector<SolvableId>{SolvableId::from_usize(1596), SolvableId::from_usize(1211)};
        auto vec2 = std::vector<ClauseId>{ClauseId::from_usize(2), ClauseId::null()};
        assert(std::vector(clause1_copy.watched_literals_.begin(), clause1_copy.watched_literals_.end()) == vec1);
        assert(std::vector(clause1_copy.next_watches_.begin(), clause1_copy.next_watches_.end()) == vec2);
    }
}


void test_requires_with_and_without_conflict() {
    auto decisions = DecisionTracker();
    auto parent = SolvableId::from_usize(1);
    auto candidate1 = SolvableId::from_usize(2);
    auto candidate2 = SolvableId::from_usize(3);

    // No conflict, all candidates available
    auto [clause, conflict] = ClauseState::requires(
        parent,
        VersionSetId::from_usize(0),
        {candidate1, candidate2},
        decisions
    );
    assert(!conflict);
    assert(clause.watched_literals_[0] == parent);
    assert(clause.watched_literals_[1] == candidate1);

    // No conflict, still one candidate available
    decisions.get_map().set(candidate1, false, 1);
    auto [clause2, conflict2] = ClauseState::requires(
        parent,
        VersionSetId::from_usize(0),
        {candidate1, candidate2},
        decisions
    );
    assert(!conflict2);
    assert(clause2.watched_literals_[0] == parent);
    assert(clause2.watched_literals_[1] == candidate2);

    // Conflict, no candidates available
    decisions.get_map().set(candidate2, false, 1);
    auto [clause3, conflict3] = ClauseState::requires(
        parent,
        VersionSetId::from_usize(0),
        {candidate1, candidate2},
        decisions
    );
    assert(conflict3);
    assert(clause3.watched_literals_[0] == parent);
    assert(clause3.watched_literals_[1] == candidate1);

    // Panic
    decisions.get_map().set(parent, false, 1);
    bool panicked = false;
    try {
        ClauseState::requires(
            parent,
            VersionSetId::from_usize(0),
            {candidate1, candidate2},
            decisions
        );
    } catch (const std::exception& e) {
        panicked = true;
    }
    assert(panicked);
}

void test_constrains_with_and_without_conflict() {
    auto decisions = DecisionTracker();

    auto parent = SolvableId::from_usize(1);
    auto forbidden = SolvableId::from_usize(2);

    // No conflict, forbidden package not installed
    auto [clause, conflict] = ClauseState::constrains(parent, forbidden, VersionSetId::from_usize(0), decisions);
    assert(!conflict);
    assert(clause.watched_literals_[0] == parent);
    assert(clause.watched_literals_[1] == forbidden);

    // Conflict, forbidden package installed
    decisions.try_add_decision(Decision{forbidden, true, ClauseId::null()}, 1);
    auto [clause2, conflict2] = ClauseState::constrains(parent, forbidden, VersionSetId::from_usize(0), decisions);
    assert(conflict2);
    assert(clause2.watched_literals_[0] == parent);
    assert(clause2.watched_literals_[1] == forbidden);

    // Panic
    decisions.try_add_decision(Decision{parent, false, ClauseId::null()}, 1);
    bool panicked = false;
    try {
        ClauseState::constrains(parent, forbidden, VersionSetId::from_usize(0), decisions);
    } catch (const std::exception& e) {
        panicked = true;
    }
    assert(panicked);
}

void test_clause_size() {
    fprintf(stderr, "size: %lu\n", sizeof(ClauseState));
    assert(sizeof(ClauseState) == 32);
}

int ttrek_PretendSubCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {

//    test_literal_satisfying_value();
//    test_literal_eval();
//    test_unlink_clause_different();
//    test_unlink_clause_same();
//    test_requires_with_and_without_conflict();
//    test_constrains_with_and_without_conflict();
//    test_clause_size();

//    test_unit_propagation_1();
//    test_unit_propagation_nested();
//    test_resolve_multiple();
//    test_resolve_with_concurrent_metadata_fetching();
    test_resolve_with_conflict();

    return TCL_OK;
}

