#include <utility>
#include <vector>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include "../internal/SolvableId.h"
#include "../internal/NameId.h"
#include "../internal/VersionSetId.h"
#include "../internal/ClauseId.h"
#include "../internal/LearntClauseId.h"
#include "../internal/Arena.h"
#include "../Pool.h"
#include "../Common.h"
#include "Clause.h"
#include "WatchMap.h"
#include "../Problem.h"
#include "../internal/tracing.h"
#include "UnsolvableOrCancelled.h"
#include "PropagationError.h"
#include "SolverCache.h"
#include <memory>
#include <any>
#include <set>

template<typename SolvableId, typename VersionSetId, typename ClauseId>
struct AddClauseOutput {
    std::vector<std::tuple<SolvableId, VersionSetId, ClauseId>> new_requires_clauses;
    std::vector<ClauseId> conflicting_clauses;
    std::vector<std::tuple<SolvableId, ClauseId>> negative_assertions;
    std::vector<ClauseId> clauses_to_watch;
};

template<typename LearntClauseId, typename ClauseId>
class Mapping;


// NowOrNeverRuntime is a placeholder for the actual async runtime type
using NowOrNeverRuntime = void; // Replace with actual async runtime type

namespace TaskResult {
    struct Dependencies {
        SolvableId solvable_id;
        DependenciesVariant dependencies;
    };

    struct SortedCandidates {
        SolvableId solvable_id;
        VersionSetId version_set_id;
        std::vector<SolvableId> candidates;
    };

    struct NonMatchingCandidates {
        SolvableId solvable_id;
        VersionSetId version_set_id;
        std::vector<SolvableId> non_matching_candidates;
    };

    struct Candidates {
        NameId name_id;
        PackageCandidates package_candidates;
    };
}

using TaskResultVariant = std::variant<TaskResult::Dependencies, TaskResult::SortedCandidates, TaskResult::NonMatchingCandidates, TaskResult::Candidates>;

// Drives the SAT solving process
//template<typename VS, typename N, typename D, typename RT = NowOrNeverRuntime>
template<typename VS, typename N, typename D>
class Solver {
private:
    std::vector<std::tuple<SolvableId, VersionSetId, ClauseId>> requires_clauses_;
    WatchMap watches_;

    std::vector<std::tuple<SolvableId, ClauseId>> negative_assertions_;

    Arena<LearntClauseId, std::vector<Literal>> learnt_clauses_;
    std::unordered_map<LearntClauseId, std::vector<ClauseId>> learnt_why_;
    std::vector<ClauseId> learnt_clause_ids_;

    std::unordered_set<NameId> clauses_added_for_package_;
    std::unordered_set<SolvableId> clauses_added_for_solvable_;

    DecisionTracker decision_tracker_;

// The version sets that must be installed as part of the solution.
    std::vector<VersionSetId> root_requirements_;

public:

    // The Pool used by the solver
    std::shared_ptr<Pool<VS, N>> pool;
//    RT async_runtime;
    SolverCache<VS, N, D> cache;
    Arena<ClauseId, ClauseState> clauses_;

    // Constructor
    explicit Solver(D provider) :
            pool(provider.pool), /* async_runtime(NowOrNeverRuntime()), */
            cache(SolverCache<VS, N, D>(provider)) {}

    std::pair<std::vector<SolvableId>, std::optional<UnsolvableOrCancelledVariant>>
    solve(std::vector<VersionSetId> root_requirements) {
        // Clear state
        decision_tracker_.clear();
        negative_assertions_.clear();
        learnt_clauses_.clear();
        root_requirements_ = std::move(root_requirements);

        // The first clause will always be the install root clause. Here we verify that this is
        // indeed the case.
        auto root_clause = clauses_.alloc(ClauseState::root());
        assert(root_clause == ClauseId::install_root());

        // Run SAT
        auto err = run_sat();
        std::vector<SolvableId> steps;
        if (err.has_value()) {
            return std::make_pair(steps, err);
        }

        for (const auto &d: decision_tracker_.get_stack()) {
            if (d.value && d.solvable_id != SolvableId::root()) {
                steps.push_back(d.solvable_id);
            }
            // Ignore things that are set to false
        }
        return std::make_pair(steps, err);
    }

    // Adds clauses for a solvable. These clauses include requirements and constrains on other
    // solvables.
    //
    // Returns the added clauses, and an additional list with conflicting clauses (if any).
    //
    // If the provider has requested the solving process to be cancelled, the cancellation value
    // will be returned as an `Err(...)`.
    AddClauseOutput<SolvableId, VersionSetId, ClauseId> add_clauses_for_solvables(
            const std::vector<SolvableId> &solvable_ids) {

        auto output = AddClauseOutput<SolvableId, VersionSetId, ClauseId>();

        // Mark the initial seen solvables as seen
        std::vector<SolvableId> pending_solvables;
        for (auto &solvable_id: solvable_ids) {
            if (clauses_added_for_solvable_.insert(solvable_id).second) {
                pending_solvables.push_back(solvable_id);
            }
        }


        std::unordered_set<SolvableId> seen(pending_solvables.cbegin(), pending_solvables.cend());
        std::vector<TaskResultVariant> pending_futures;
        while (true) {
            // Iterate over all pending solvables and request their dependencies.
            auto drained_solvables = pending_solvables;
            pending_solvables.clear();

            for (SolvableId solvable_id: drained_solvables) {
                // Get the solvable information and request its requirements and constraints
                auto solvable = pool->resolve_internal_solvable(solvable_id);

                auto display_solvable = DisplaySolvable(pool, solvable);
                tracing::trace(
                        "┝━ adding clauses for dependencies of %s\n",
                        display_solvable.to_string().c_str()
                );

                auto optional_get_dependencies_fut = std::visit(
                        [this, &solvable_id](const auto &arg) -> std::optional<TaskResultVariant> {
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, SolvableInner::Root>) {
                                auto known_dependencies = KnownDependencies{root_requirements_,
                                                                            std::vector<VersionSetId>()};
                                return std::optional(
                                        TaskResult::Dependencies{solvable_id, Dependencies::Known{known_dependencies}});
                            } else if constexpr (std::is_same_v<T, SolvableInner::Package<typename VS::ValueType>>) {
                                auto deps = cache.get_or_cache_dependencies(solvable_id);
                                return std::optional(TaskResult::Dependencies{solvable_id, deps});
                            }
                            return std::nullopt;
                        }, solvable.inner);

                if (optional_get_dependencies_fut.has_value()) {
                    pending_futures.emplace_back(optional_get_dependencies_fut.value());
                }
            }

            if (pending_futures.empty()) {
                // No more pending results
                fprintf(stderr, "no more pending results\n");
                break;
            }

            auto result = pending_futures.front();  // pending_futures.next()
            pending_futures.erase(pending_futures.begin());  // pending_futures.pop_front()

            auto continue_p = std::visit([this, &output, &pending_futures, &pending_solvables, &seen](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, TaskResult::Dependencies>) {
                    auto task_result = std::any_cast<TaskResult::Dependencies>(arg);
                    auto solvable_id = task_result.solvable_id;
                    auto dependencies = task_result.dependencies;

                    auto solvable = pool->resolve_internal_solvable(solvable_id);


                    auto display_solvable = DisplaySolvable(pool, solvable);
                    tracing::trace(
                            "dependencies available for %s\n",
                            display_solvable.to_string().c_str()
                    );

                    auto continue_inner_p = std::visit(
                            [this, &output, &pending_futures, &solvable_id](auto &&arg_inner) {
                                using T_INNER = std::decay_t<decltype(arg_inner)>;

                                auto requirements = std::vector<VersionSetId>();
                                auto constrains = std::vector<VersionSetId>();
                                if constexpr (std::is_same_v<T_INNER, Dependencies::Known>) {
                                    auto known_dependencies = std::any_cast<Dependencies::Known>(
                                            arg_inner).known_dependencies;
                                    requirements = known_dependencies.requirements;
                                    constrains = known_dependencies.constrains;
                                } else if constexpr (std::is_same_v<T_INNER, Dependencies::Unknown>) {
                                    auto reason = std::any_cast<Dependencies::Unknown>(arg_inner).reason;

                                    fprintf(stderr, "unknown dependencies: %lu\n", reason.to_usize());

                                    // There is no information about the solvable's dependencies, so we add
                                    // an exclusion clause for it
                                    auto clause_id = clauses_.alloc(ClauseState::exclude(solvable_id, reason));

                                    // Exclusions are negative assertions, tracked outside of the watcher system
                                    output.negative_assertions.emplace_back(solvable_id, clause_id);

                                    // There might be a conflict now
                                    if (decision_tracker_.assigned_value(solvable_id).has_value()) {
                                        output.conflicting_clauses.push_back(clause_id);
                                    }

                                    return true;  // continue
                                }


                                std::vector<VersionSetId> chain;
                                chain.insert(chain.end(), requirements.begin(), requirements.end());
                                chain.insert(chain.end(), constrains.begin(), constrains.end());
                                for (auto &version_set_id: chain) {
                                    auto dependency_name = pool->resolve_version_set_package_name(version_set_id);

                                    if (clauses_added_for_package_.insert(dependency_name).second) {
                                        tracing::trace(
                                                "┝━ adding clauses for package '%s'\n",
                                                pool->resolve_package_name(dependency_name).c_str()
                                        );

                                        auto package_candidates = cache.get_or_cache_candidates(dependency_name);
                                        fprintf(stderr,
                                                "add_clauses_for_solvables: package_candidates.candidates size: %zd\n",
                                                package_candidates.candidates.size());
                                        pending_futures.emplace_back(
                                                TaskResult::Candidates{dependency_name, package_candidates});
                                    }
                                }

                                for (VersionSetId version_set_id: requirements) {
                                    // Find all the solvable that match for the given version set
                                    auto sorted_candidates = cache.get_or_cache_sorted_candidates(version_set_id);
                                    fprintf(stderr, "add_clauses_for_solvables: sorted_candidates size: %zd\n",
                                            sorted_candidates.size());
                                    pending_futures.emplace_back(
                                            TaskResult::SortedCandidates{solvable_id, version_set_id,
                                                                         sorted_candidates});
                                }

                                for (VersionSetId version_set_id: constrains) {
                                    // Find all the solvables that match for the given version set
                                    auto non_matching_candidates = cache.get_or_cache_non_matching_candidates(
                                            version_set_id);
                                    pending_futures.emplace_back(
                                            TaskResult::NonMatchingCandidates{solvable_id, version_set_id,
                                                                              non_matching_candidates});
                                }

                                return false;
                            }, dependencies);

                    if (continue_inner_p) {
                        return true;  // continue
                    }

                    return false;  // normal flow
                } else if constexpr (std::is_same_v<T, TaskResult::Candidates>) {
                    auto task_result = std::any_cast<TaskResult::Candidates>(arg);
                    auto name_id = task_result.name_id;
                    auto package_candidates = task_result.package_candidates;
                    auto solvable = pool->resolve_package_name(name_id);
                    // tracing::trace!("package candidates available for {}", solvable);

                    auto locked_solvable_id = package_candidates.locked;
                    auto candidates = package_candidates.candidates;

                    // Check the assumption that no decision has been made about any of the solvables.
                    for (auto &candidate: candidates) {
                        // "a decision has been made about a candidate of a package that was not properly added yet."
                        assert(!decision_tracker_.assigned_value(candidate).has_value());
                    }

                    // Each candidate gets a clause to disallow other candidates.

                    for (int i = 0; i < candidates.size(); i++) {
                        for (int j = i + 1; j < candidates.size(); j++) {
                            auto clause_id = clauses_.alloc(ClauseState::forbid_multiple(candidates[i], candidates[j]));
                            assert(clauses_[clause_id].has_watches());
                            output.clauses_to_watch.push_back(clause_id);
                        }
                    }

                    // If there is a locked solvable, forbid other solvables.

                    if (locked_solvable_id.has_value()) {
                        for (auto &other_candidate: candidates) {
                            if (other_candidate != locked_solvable_id.value()) {
                                auto clause_id = clauses_.alloc(
                                        ClauseState::lock(locked_solvable_id.value(), other_candidate));
                                assert(clauses_[clause_id].has_watches());
                                output.clauses_to_watch.push_back(clause_id);
                            }
                        }
                    }

                    // Add a clause for solvables that are externally excluded.

                    for (auto &[excluded_solvable, reason]: package_candidates.excluded) {
                        auto clause_id = clauses_.alloc(ClauseState::exclude(excluded_solvable, reason));

                        fprintf(stderr, "excluded solvable: %d\n", excluded_solvable.to_usize());

                        // Exclusions are negative assertions, tracked outside of the watcher system
                        output.negative_assertions.push_back({excluded_solvable, clause_id});

                        // Conflicts should be impossible here
                        //                        debug_assert!(self.decision_tracker.assigned_value(solvable) != Some(true));
                        assert(decision_tracker_.assigned_value(excluded_solvable).has_value());
                    }
                } else if constexpr (std::is_same_v<T, TaskResult::SortedCandidates>) {
                    auto task_result = std::any_cast<TaskResult::SortedCandidates>(arg);
                    auto solvable_id = task_result.solvable_id;
                    auto version_set_id = task_result.version_set_id;
                    auto candidates = task_result.candidates;

                    auto version_set_name = pool->resolve_package_name(
                            pool->resolve_version_set_package_name(version_set_id));

                    auto version_set = pool->resolve_version_set(version_set_id);

                    tracing::trace(
                            "sorted candidates available for %s %d\n",
                            version_set_name.c_str(),
                            candidates.size()
//                                            version_set
                    );

                    // Queue requesting the dependencies of the candidates as well if they are cheaply
                    // available from the dependency provider.

                    for (auto &candidate: candidates) {
                        auto display_solvable = DisplaySolvable(pool, pool->resolve_internal_solvable(candidate));
                        fprintf(stderr, "add_clauses_for_solvables: candidate: %s\n", display_solvable.to_string().c_str());
                        if (seen.insert(candidate).second && cache.are_dependencies_available_for(candidate) &&
                            clauses_added_for_solvable_.insert(candidate).second) {
                            pending_solvables.emplace_back(candidate);
                        }
                    }

                    // Add the requirements clause
                    auto no_candidates = candidates.empty();
                    auto [requires_clause, conflict] = ClauseState::requires(
                            solvable_id,
                            version_set_id,
                            candidates,
                            decision_tracker_
                    );

                    auto clause_id = clauses_.alloc(requires_clause);
                    auto clause = clauses_[clause_id];

                    //let &Clause::Requires(solvable_id, version_set_id) = &clause.kind else {
                    //                        unreachable!();
                    //                    };

                    fprintf(stderr, "solvable_id: %lu\n", solvable_id.to_usize());
                    fprintf(stderr, "clause_id: %lu\n", clause_id.to_usize());

                    if (clause.has_watches()) {
                        output.clauses_to_watch.push_back(clause_id);
                    }

                    output.new_requires_clauses.push_back({solvable_id, version_set_id, clause_id});

                    if (conflict) {
                        output.conflicting_clauses.push_back(clause_id);
                    } else if (no_candidates) {
                        // Add assertions for unit clauses (i.e. those with no matching candidates)
                        output.negative_assertions.push_back({solvable_id, clause_id});
                    }

                } else if constexpr (std::is_same_v<T, TaskResult::NonMatchingCandidates>) {
                    auto task_result = std::any_cast<TaskResult::NonMatchingCandidates>(arg);
                    auto solvable_id = task_result.solvable_id;
                    auto version_set_id = task_result.version_set_id;
                    auto non_matching_candidates = task_result.non_matching_candidates;

                    auto version_set_name = pool->resolve_package_name(
                            pool->resolve_version_set_package_name(version_set_id));
                    auto version_set = pool->resolve_version_set(version_set_id);

                    tracing::trace(
                            "non matching candidates available for %s %s\n",
                            version_set_name.c_str(),
                            "version_set"
                    );

                    // Add forbidden clauses for the candidates
                    for (auto &forbidden_candidate: non_matching_candidates) {
                        auto [constrains_clause, conflict] =
                                ClauseState::constrains(solvable_id, forbidden_candidate, version_set_id,
                                                        decision_tracker_);
                        auto clause_id = clauses_.alloc(constrains_clause);
                        output.clauses_to_watch.push_back(clause_id);

                        if (conflict) {
                            output.conflicting_clauses.push_back(clause_id);
                        }
                    }
                }

                return false;
            }, result);

            if (continue_p) {
                fprintf(stderr, "continue_p=true\n");
                continue;
            }

            fprintf(stderr, "here\n");

        }

        return output;

    }

    // Run the CDCL algorithm to solve the SAT problem
    //
    // The CDCL algorithm's job is to find a valid assignment to the variables involved in the
    // provided clauses. It works in the following steps:
    //
    // 1. __Set__: Assign a value to a variable that hasn't been assigned yet. An assignment in
    //    this step starts a new "level" (the first one being level 1). If all variables have been
    //    assigned, then we are done.
    // 2. __Propagate__: Perform [unit
    //    propagation](https://en.wikipedia.org/wiki/Unit_propagation). Assignments in this step
    //    are associated to the same "level" as the decision that triggered them. This "level"
    //    metadata is useful when it comes to handling conflicts. See [`Solver::propagate`] for the
    //    implementation of this step.
    // 3. __Learn__: If propagation finishes without conflicts, go back to 1. Otherwise find the
    //    combination of assignments that caused the conflict and add a new clause to the solver to
    //    forbid that combination of assignments (i.e. learn from this mistake so it is not
    //    repeated in the future). Then backtrack and go back to step 1 or, if the learnt clause is
    //    in conflict with existing clauses, declare the problem to be unsolvable. See
    //    [`Solver::analyze`] for the implementation of this step.
    //
    // The solver loop can be found in [`Solver::resolve_dependencies`].

    std::optional<UnsolvableOrCancelledVariant> run_sat() {
        assert(decision_tracker_.is_empty());
        int level = 0;

        while (true) {
            // A level of 0 means the decision loop has been completely reset because a partial
            // solution was invalidated by newly added clauses.
            if (level == 0) {
                // Level 1 is the initial decision level
                level = 1;

                // Assign `true` to the root solvable. This must be installed to satisfy the solution.
                // The root solvable contains the dependencies that were injected when calling
                // `Solver::solve`. If we can find a solution were the root is installable we found a
                // solution that satisfies the user requirements.

//  todo:               auto display_solvable = DisplaySolvable(*pool_ptr, SolvableId::root());
                tracing::info(
                        "╤══ install root at level %d\n",
                        level
                );

                decision_tracker_.try_add_decision(Decision(SolvableId::root(), true, ClauseId::install_root()), level);

                auto add_clauses_output = add_clauses_for_solvables({SolvableId::root()});
                fprintf(stderr, "add_clauses_output\n");
                auto optional_clause_id = process_add_clause_output(add_clauses_output);
                if (optional_clause_id.has_value()) {
                    fprintf(stderr, "optional_clause_id\n");
                    return UnsolvableOrCancelled::Unsolvable{analyze_unsolvable(optional_clause_id.value())};
                }
            }

            // Propagate decisions from assignments above
            auto propagate_result = propagate(level);

            fprintf(stderr, "run_sat: level=%d propagate_result.has_value(): %d\n", level,
                    propagate_result.has_value());

            if (propagate_result.has_value()) {
                fprintf(stderr, "handle propagation errors\n");
                // Handle propagation errors
                auto optional_err = std::visit(
                        [this, &level](const auto &arg) -> std::optional<UnsolvableOrCancelledVariant> {
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, PropagationError::Conflict>) {
                                auto arg_conflict = std::any_cast<PropagationError::Conflict>(arg);
                                auto [_1, _2, clause_id] = arg_conflict;
                                if (level == 1) {
                                    fprintf(stderr, "conflict\n");
                                    return std::optional(
                                            UnsolvableOrCancelled::Unsolvable{analyze_unsolvable(clause_id)});
                                } else {
                                    // The conflict was caused because new clauses have been added dynamically.
                                    // We need to start over.
                                    tracing::debug(
                                            "├─ added clause {clause:?} introduces a conflict which invalidates the partial solution\n");
                                    level = 0;
                                    decision_tracker_.clear();
                                    return std::nullopt;  // continue
                                }
                            } else if constexpr (std::is_same_v<T, PropagationError::Cancelled>) {
                                auto arg_cancelled = std::any_cast<PropagationError::Cancelled>(arg);
                                return std::optional(UnsolvableOrCancelled::Cancelled{arg_cancelled});
                            }
                            return std::nullopt;
                        }, propagate_result.value());

                if (optional_err.has_value()) {
                    fprintf(stderr, "optional_err has value\n");
                    return optional_err;
                } else {
                    continue;
                }
            }

            // Enter the solver loop, return immediately if no new assignments have been made.
            auto [resolved_level, err] = resolve_dependencies(level);
            if (err.has_value()) {
                return err;
            }
            level = resolved_level;

            // We have a partial solution. E.g. there is a solution that satisfies all the clauses
            // that have been added so far.

            fprintf(stderr, "run_sat: level=%d partial solution\n", level);

            // Determine which solvables are part of the solution for which we did not yet get any
            // dependencies. If we find any such solvable it means we did not arrive at the full
            // solution yet.

            std::vector<std::tuple<SolvableId, ClauseId>> new_solvables;
            for (const auto &d: decision_tracker_.get_stack()) {
                auto display_solvable = DisplaySolvable(pool, pool->resolve_internal_solvable(d.solvable_id));
                fprintf(stderr, "decision_tracker_.get_stack(): %s d.value=%d\n", display_solvable.to_string().c_str(),
                        d.value);
                if (d.value && clauses_added_for_solvable_.find(d.solvable_id) == clauses_added_for_solvable_.end()) {
                    // Filter only decisions that led to a positive assignment
                    // Select solvables for which we do not yet have dependencies
                    new_solvables.emplace_back(d.solvable_id, d.derived_from);
                }
            }

            fprintf(stderr, "new_solvables.size(): %zd\n", new_solvables.size());

            if (new_solvables.empty()) {
                // If no new literals were selected this solution is complete and we can return.
                fprintf(stderr, "run_sat: level=%d complete solution\n", level);
                return std::nullopt;
            }
            // tracing::debug!(
            //                "====\n==Found newly selected solvables\n- {}\n====",
            //                new_solvables
            //                    .iter()
            //                    .copied()
            //                    .format_with("\n- ", |(id, derived_from), f| f(&format_args!(
            //                        "{} (derived from {:?})",
            //                        id.display(&self.pool),
            //                        self.clauses.borrow()[derived_from].debug(&self.pool),
            //                    )))
            //            );
            // in c++ it should be:

            tracing::debug("====\n==Found newly selected solvables\n");
//            for (auto &[solvable_id, derived_from]: new_solvables) {
//                auto display_solvable = DisplaySolvable(*pool_ptr, solvable_id);
//                auto display_clause = DisplayClause(*pool, clauses_[derived_from]);
//                tracing::debug(
//                                    "- %s (derived from %s)\n",
//                                    display_solvable.to_string().c_str(),
//                                    display_clause.to_string().c_str()
//                                );
//            }

            // Concurrently get the solvable's clauses
            auto new_solvable_ids = std::vector<SolvableId>();
            for (auto &[solvable_id, derived_from]: new_solvables) {
                new_solvable_ids.push_back(solvable_id);
            }
            auto output = add_clauses_for_solvables(new_solvable_ids);

            // Serially process the outputs, to reduce the need for synchronization
            for (auto &clause_id: output.conflicting_clauses) {
                tracing::debug("├─ added clause %lu introduces a conflict which invalidates the partial solution\n",
//                        clause=self.clauses.borrow()[clause_id].debug(&self.pool)
//                               "clauses[clause_id]"
                               clause_id.to_usize()
                );
            }

            auto optional_conflicting_clause_id = process_add_clause_output(output);
            if (optional_conflicting_clause_id.has_value()) {
                decision_tracker_.clear();
                level = 0;
            }
        }
    }

    std::optional<ClauseId> process_add_clause_output(AddClauseOutput<SolvableId, VersionSetId, ClauseId> output) {
        for (const auto &clause_id: output.clauses_to_watch) {
            assert(clauses_[clause_id].has_watches());
            watches_.start_watching(clauses_[clause_id], clause_id);
        }

        fprintf(stderr, "new_requires_clauses: %zd\n", output.new_requires_clauses.size());
        requires_clauses_.insert(requires_clauses_.end(), output.new_requires_clauses.begin(),
                                 output.new_requires_clauses.end());

        fprintf(stderr, "negative_assertions: %zd\n", output.negative_assertions.size());
        negative_assertions_.insert(negative_assertions_.end(), output.negative_assertions.begin(),
                                    output.negative_assertions.end());

        if (!output.conflicting_clauses.empty()) {
            fprintf(stderr, "conflicting clauses\n");
            return {output.conflicting_clauses[0]};
        }

        return std::nullopt;
    }

    // Resolves all dependencies
    //
    // Repeatedly chooses the next variable to assign, and calls [`Solver::set_propagate_learn`] to
    // drive the solving process (as you can see from the name, the method executes the set,
    // propagate and learn steps described in the [`Solver::run_sat`] docs).
    //
    // The next variable to assign is obtained by finding the next dependency for which no concrete
    // package has been picked yet. Then we pick the highest possible version for that package, or
    // the favored version if it was provided by the user, and set its value to true.

    std::pair<uint32_t, std::optional<UnsolvableOrCancelledVariant>> resolve_dependencies(uint32_t level) {
        fprintf(stderr, "resolve_dependencies: level=%d\n", level);
        while (true) {
            // Make a decision. If no decision could be made it means the problem is satisfyable.
            auto decision = decide();
            if (!decision.has_value()) {
                break;
            }

            auto [candidate, required_by, clause_id] = decision.value();

            // Propagate the decision
            auto [new_level, optional_unsolvable] = set_propagate_learn(level, candidate, required_by, clause_id);
            if (optional_unsolvable.has_value()) {
                fprintf(stderr, "resolve_dependencies: set_propagate_learn->optional_unsolvable\n");
                return {new_level, optional_unsolvable};
            }

            level = new_level;
        }

        // We just went through all clauses and there are no choices left to be made
        return {level, std::nullopt};
    }

    // Pick a solvable that we are going to assign true. This function uses a heuristic to
    // determine to best decision to make. The function selects the requirement that has the least
    // amount of working available candidates and selects the best candidate from that list. This
    // ensures that if there are conflicts they are dealt with as early as possible.

    std::optional<std::tuple<SolvableId, SolvableId, ClauseId>> decide() {
        std::pair<uint32_t, std::optional<std::tuple<SolvableId, SolvableId, ClauseId>>> best_decision;
        for (auto &[solvable_id, deps, clause_id]: requires_clauses_) {
            auto assigned_value = decision_tracker_.assigned_value(solvable_id);
            if (!assigned_value.has_value() || !assigned_value.value()) {
                continue;
            }

            // Consider only clauses in which no candidates have been installed
            auto optional_candidates = cache.version_set_to_sorted_candidates.get(deps);

            std::optional<SolvableId> first_selectable_candidate;
            int selectable_candidates = 0;
            for (auto &candidate: optional_candidates.value()) {
                auto optional_assigned_value = decision_tracker_.assigned_value(candidate);
                if (optional_assigned_value.has_value()) {
                    if (optional_assigned_value.value()) {
                        break;
                    }
                } else {
                    first_selectable_candidate = candidate;
                    selectable_candidates++;
                }
            }

            if (first_selectable_candidate.has_value()) {
                auto possible_decision = std::make_tuple(first_selectable_candidate.value(), solvable_id, clause_id);
                if (!best_decision.second.has_value() || selectable_candidates < best_decision.first) {
                    best_decision = std::make_pair(selectable_candidates, possible_decision);
                }
            }
        }

        auto [count, the_decision] = best_decision;
        if (the_decision.has_value()) {
            auto [candidate, _solvable_id, clause_id] = the_decision.value();
            tracing::info("deciding to assign: candidate=%zd solvable_id=%zd clause_id=%zd\n", candidate.to_usize(),
                          _solvable_id.to_usize(), clause_id.to_usize());
            // tracing::info!(
            //                "deciding to assign {}, ({:?}, {} possible candidates)",
            //                candidate.display(&self.pool),
            //                self.clauses.borrow()[clause_id].debug(&self.pool),
            //                count,
            //            );

            return the_decision;
        }

        // Could not find a requirement that needs satisfying.
        return std::nullopt;
    }

    // Executes one iteration of the CDCL loop
    //
    // A set-propagate-learn round is always initiated by a requirement clause (i.e.
    // [`Clause::Requires`]). The parameters include the variable associated to the candidate for the
    // dependency (`solvable`), the package that originates the dependency (`required_by`), and the
    // id of the requires clause (`clause_id`).
    //
    // Refer to the documentation of [`Solver::run_sat`] for details on the CDCL algorithm.
    //
    // Returns the new level after this set-propagate-learn round, or a [`Problem`] if we
    // discovered that the requested jobs are unsatisfiable.
    std::pair<uint32_t, std::optional<UnsolvableOrCancelledVariant>>
    set_propagate_learn(uint32_t level, const SolvableId &solvable, const SolvableId &required_by,
                        const ClauseId &clause_id) {
        level += 1;

        tracing::info("propagate_and_learn solvable=%lu required_by=%lu\n", solvable.to_usize(),
                      required_by.to_usize());

        auto display_solvable = DisplaySolvable(pool, pool->resolve_internal_solvable(solvable));
        auto display_required_by = DisplaySolvable(pool, pool->resolve_internal_solvable(required_by));
        tracing::info(
                "╤══ Install %s at level %d (required by %s)\n",
                display_solvable.to_string().c_str(),
                level,
                display_required_by.to_string().c_str()
        );


        // Add the decision to the tracker
        auto optional_decision = decision_tracker_.try_add_decision(Decision(solvable, true, clause_id), level);
        if (!optional_decision.has_value()) {
            // bug: solvable was already decided!
            fprintf(stderr, "^^^^^^^^^^^^^^^^^^^^ bug: solvable was already decided!\n");
            return std::make_pair(level, std::nullopt);
        }

        return propagate_and_learn(level);
    }

    std::pair<uint32_t, std::optional<UnsolvableOrCancelledVariant>> propagate_and_learn(uint32_t level) {
        while (true) {
            auto propagate_result = propagate(level);
            if (!propagate_result.has_value()) {
                // Propagation completed
                tracing::debug("╘══ Propagation completed\n");
                return {level, std::nullopt};
            }

            auto [output_level, optional_err] = std::visit(
                    [this, &level](auto &&arg) -> std::pair<uint32_t, std::optional<UnsolvableOrCancelledVariant>> {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, PropagationError::Cancelled>) {
                            // Propagation cancelled
                            auto err = std::any_cast<PropagationError::Cancelled>(arg);
                            tracing::debug("╘══ Propagation cancelled\n");
                            return std::make_pair(level, std::optional(UnsolvableOrCancelled::Cancelled{err.data}));
                        } else if constexpr (std::is_same_v<T, PropagationError::Conflict>) {
                            auto err = std::any_cast<PropagationError::Conflict>(arg);
                            auto [conflicting_solvable, attempted_value, conflicting_clause] = err;
                            auto [new_level, optional_problem] = learn_from_conflict(level, conflicting_solvable,
                                                                                     attempted_value,
                                                                                     conflicting_clause);
                            if (optional_problem.has_value()) {
                                return std::make_pair(new_level, std::optional(
                                        UnsolvableOrCancelled::Unsolvable{optional_problem.value()}));
                            } else {
                                return std::make_pair(new_level, std::nullopt);
                            }
                        }
                        return std::make_pair(level, std::nullopt);
                    }, propagate_result.value());

            if (optional_err.has_value()) {
                return std::make_pair(output_level, optional_err);
            }

            level = output_level;
        }
    }

    std::pair<uint32_t, std::optional<Problem>>
    learn_from_conflict(uint32_t level, const SolvableId &conflicting_solvable,
                        bool attempted_value, const ClauseId &conflicting_clause) {

        tracing::info("learn_from_conflict\n");

        auto display_solvable = DisplaySolvable(pool, pool->resolve_internal_solvable(conflicting_solvable));
        tracing::info(
                "├─ Propagation conflicted: could not set %s to %d",
                display_solvable.to_string().c_str(),
                attempted_value
        );

        tracing::info(
                "│  During unit propagation for clause: {}\n"
//                        self.clauses.borrow()[conflicting_clause].debug(&self.pool)
        );

        tracing::info(
                "│  Previously decided value: %d. Derived from: {}",
                !attempted_value
//                clauses_[decision_tracker_.find_clause_for_assignment(conflicting_solvable).value()]
        );

        if (level == 1) {
            tracing::info("╘══ UNSOLVABLE");

            for (const auto &decision: decision_tracker_.get_stack()) {
                auto clause = clauses_[decision.derived_from];
                auto decision_level = decision_tracker_.get_level(decision.solvable_id);
                auto decision_action = decision.value ? "install" : "forbid";

                if (std::holds_alternative<Clause::ForbidMultipleInstances>(clause.get_kind())) {
                    // Skip forbids clauses, to reduce noise
                    continue;
                }

                //                tracing::info!(
                //                    "* ({level}) {action} {}. Reason: {:?}",
                //                    decision.solvable_id.display(&self.pool),
                //                    clause.debug(&self.pool),
                //                );
            }
            return {level, std::optional(analyze_unsolvable(conflicting_clause))};
        }

        auto [new_level, learned_clause_id, literal] = analyze(level, conflicting_solvable, conflicting_clause);
        level = new_level;

        tracing::debug("├─ Backtracked to level %d", level);

        // Optimization: propagate right now, since we know that the clause is a unit clause
        auto decision = literal.satisfying_value();
        decision_tracker_.try_add_decision(Decision(literal.solvable_id, decision, learned_clause_id), level);
        tracing::debug(
                "├─ Propagate after learn: {} = {decision}"
//            literal.solvable_id.display(&self.pool)
        );
        return std::make_pair(level, std::nullopt);
    };


    // The propagate step of the CDCL algorithm
    //
    // Propagation is implemented by means of watches: each clause that has two or more literals is
    // "subscribed" to changes in the values of two solvables that appear in the clause. When a value
    // is assigned to a solvable, each of the clauses tracking that solvable will be notified. That
    // way, the clause can check whether the literal that is using the solvable has become false, in
    // which case it picks a new solvable to watch (if available) or triggers an assignment.
    //fn propagate(&mut self, level: u32) -> Result<(), PropagationError> {

    std::optional<PropagationErrorVariant> propagate(uint32_t level) {
        fprintf(stderr, "propagate: propagate level: %d\n", level);
        auto optional_value = cache.provider.should_cancel_with_value();
        fprintf(stderr, "propagate: optional_value.has_value(): %d\n", optional_value.has_value() ? 1 : 0);
        if (optional_value.has_value()) {
            return std::optional(PropagationError::Cancelled{optional_value.value()});
        }

        // Negative assertions derived from other rules (assertions are clauses that consist of a
        // single literal, and therefore do not have watches)
        for (auto &[solvable_id, clause_id]: negative_assertions_) {
            bool value = false;
            auto decided = decision_tracker_.try_add_decision(Decision(solvable_id, value, clause_id), level);
            if (!decided.has_value()) {
                fprintf(stderr, "propagate: decided has no value\n");
                return std::optional(PropagationError::Conflict{solvable_id, value, clause_id});
            }

            auto display_solvable = DisplaySolvable(pool, pool->resolve_internal_solvable(solvable_id));

            tracing::trace(
                    "├─ Propagate assertion %s = %d\n",
                    display_solvable.to_string().c_str(),
                    value
            );
        }

        fprintf(stderr, "propagate: learnt_clause_ids_.size(): %zd\n", learnt_clause_ids_.size());

        // Assertions derived from learnt rules
        for (auto &learn_clause_id: learnt_clause_ids_) {
            auto clause_id = learn_clause_id;
            auto clause = clauses_[clause_id];

            if (!std::holds_alternative<Clause::Learnt>(clause.get_kind())) {
                // unreachable
                throw std::runtime_error("Expected a learnt clause");
            }

            auto learnt_clause = std::any_cast<Clause::Learnt>(clause.get_kind());
            auto learnt_index = learnt_clause.learnt_clause_id;
            auto literals = learnt_clauses_[learnt_index];
            if (literals.size() > 1) {
                continue;
            }

            auto literal = literals[0];
            auto decision = literal.satisfying_value();

            auto decided = decision_tracker_.try_add_decision(Decision(literal.solvable_id, decision, clause_id),
                                                              level);
            if (!decided.has_value()) {
                return std::optional(PropagationError::Conflict{literal.solvable_id, decision, clause_id});
            }

            auto display_solvable = DisplaySolvable(pool, pool->resolve_internal_solvable(literal.solvable_id));
            tracing::trace(
                    "├─ Propagate assertion %s = %d",
                    display_solvable.to_string().c_str(),
                    decision
            );
        }

        fprintf(stderr, "propagate: watched solvables\n");

        // Watched solvables
        while (true) {
            auto decision = decision_tracker_.next_unpropagated();
            if (!decision.has_value()) {
                fprintf(stderr, "propagate: no decision\n");
                break;
            }

            auto pkg = decision.value().solvable_id;

            // Propagate, iterating through the linked list of clauses that watch this solvable
            std::optional<ClauseId> old_predecessor_clause_id;
            std::optional<ClauseId> predecessor_clause_id;
            auto clause_id = watches_.first_clause_watching_solvable(pkg);
            while (!clause_id.is_null()) {
                if (predecessor_clause_id == clause_id) {
                    throw std::runtime_error("Linked list is circular!");
                }

                // Get mutable access to both clauses.
                auto &clauses = clauses_;
                auto predecessor_clause = predecessor_clause_id.has_value() ? std::optional<ClauseState>(
                        clauses[predecessor_clause_id.value()]) : std::nullopt;
                auto &clause = clauses[clause_id];

                // Update the prev_clause_id for the next run
                old_predecessor_clause_id = predecessor_clause_id;
                predecessor_clause_id = std::optional<ClauseId>(clause_id);

                // Configure the next clause to visit
                auto this_clause_id = clause_id;
                clause_id = clause.next_watched_clause(pkg);


                auto optional_payload = clause.watch_turned_false(pkg, decision_tracker_.get_map(),
                                                                  learnt_clauses_);

                if (optional_payload.has_value()) {
                    auto [watched_literals, watch_index] = optional_payload.value();
                    auto optional_variable = clause.next_unwatched_variable(learnt_clauses_,
                                                                            cache.version_set_to_sorted_candidates,
                                                                            decision_tracker_.get_map());
                    if (optional_variable.has_value()) {
                        assert(std::find(clause.watched_literals_.cbegin(), clause.watched_literals_.cend(),
                                         optional_variable.value()) == clause.watched_literals_.cend());

                        watches_.update_watched(predecessor_clause, clause, this_clause_id, watch_index, pkg,
                                                optional_variable.value());

                        // Make sure the right predecessor is kept for the next iteration (i.e. the
                        // current clause is no longer a predecessor of the next one; the current
                        // clause's predecessor is)
                        predecessor_clause_id = old_predecessor_clause_id;
                    } else {
                        // We could not find another literal to watch, which means the remaining
                        // watched literal can be set to true
                        auto remaining_watch_index = watch_index == 0 ? 1 : 0;

                        auto remaining_watch = watched_literals[remaining_watch_index];

                        auto decided = decision_tracker_.try_add_decision(Decision(remaining_watch.solvable_id,
                                                                                   remaining_watch.satisfying_value(),
                                                                                   this_clause_id), level);

                        if (!decided.has_value()) {
                            return PropagationError::Conflict{remaining_watch.solvable_id, true, this_clause_id};
                        }

                        // Skip logging for ForbidMultipleInstances, which is so noisy
                        if (!std::holds_alternative<Clause::ForbidMultipleInstances>(clause.get_kind())) {
                            tracing::debug(
                                    "├─ Propagate {} = {}. {:?}"
//                                    remaining_watch.solvable_id.display(&self.cache.pool()),
//                                    remaining_watch.satisfying_value(),
//                                    clause.debug(&self.cache.pool()),
                            );
                        }
                    }
                }
            }
        }
        return std::nullopt;
    }


    // Adds the clause with `clause_id` to the current `Problem`
    //
    // Because learnt clauses are not relevant for the user, they are not added to the `Problem`.
    // Instead, we report the clauses that caused them.
    //fn analyze_unsolvable_clause(

    void analyze_unsolvable_clause(const Arena<ClauseId, ClauseState> &clauses,
                                   const std::unordered_map<LearntClauseId, std::vector<ClauseId>> &learnt_why,
                                   const ClauseId &clause_id,
                                   Problem &problem, std::unordered_set<ClauseId> &seen) {
        auto &clause = clauses[clause_id];
        return std::visit([this, &clauses, &learnt_why, &clause_id, &problem, &seen](auto &arg_clause) {
            using T = std::decay_t<decltype(arg_clause)>;
            if constexpr (std::is_same_v<T, Clause::Learnt>) {
                if (!seen.insert(clause_id).second) {
                    return;
                }

                auto clause = std::any_cast<Clause::Learnt>(arg_clause);
                auto learnt_clause_id = clause.learnt_clause_id;
                auto causes = learnt_why.at(learnt_clause_id);
                for (auto &cause: causes) {
                    analyze_unsolvable_clause(clauses, learnt_why, cause, problem, seen);
                }
            } else {
                problem.add_clause(clause_id);
            }
        }, clause.get_kind());
    }

    // Create a [`Problem`] based on the id of the clause that triggered an unrecoverable conflict
    Problem analyze_unsolvable(const ClauseId &clause_id) {
        auto last_decision = decision_tracker_.get_stack().back();
        auto highest_level = decision_tracker_.get_level(last_decision.solvable_id);
        assert(highest_level == 1);

        auto problem = Problem();

        std::unordered_set<SolvableId> involved;
        auto &clause = clauses_[clause_id];
        clause.visit_literals(learnt_clauses_, cache.version_set_to_sorted_candidates,
                              [&involved](const Literal &literal) {
                                  involved.insert(literal.solvable_id);
                              });

        std::unordered_set<ClauseId> seen;
        analyze_unsolvable_clause(clauses_, learnt_why_, clause_id, problem, seen);

        auto stack = decision_tracker_.get_stack();
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            auto &decision = *it;
            if (decision.solvable_id == SolvableId::root()) {
                continue;
            }

            auto why = decision.derived_from;

            if (involved.find(decision.solvable_id) == involved.end()) {
                continue;
            }

            assert(why != ClauseId::install_root());

            analyze_unsolvable_clause(clauses_, learnt_why_, why, problem, seen);

            auto &why_clause = clauses_[why];

            why_clause.visit_literals(learnt_clauses_, cache.version_set_to_sorted_candidates,
                                      [&decision, this, &involved](const Literal &literal) {
                                          if (literal.eval(decision_tracker_.get_map()) ==
                                              std::optional<bool>(true)) {
                                              assert(literal.solvable_id == decision.solvable_id);
                                          } else {
                                              involved.insert(literal.solvable_id);
                                          }
                                      });
        }

        return problem;
    }

    // Analyze the causes of the conflict and learn from it
    //
    // This function finds the combination of assignments that caused the conflict and adds a new
    // clause to the solver to forbid that combination of assignments (i.e. learn from this mistake
    // so it is not repeated in the future). It corresponds to the `Solver.analyze` function from
    // the MiniSAT paper.
    //
    // Returns the level to which we should backtrack, the id of the learnt clause and the literal
    // that should be assigned (by definition, when we learn a clause, all its literals except one
    // evaluate to false, so the value of the remaining literal must be assigned to make the clause
    // become true)

    std::tuple<uint32_t, ClauseId, Literal> analyze(uint32_t current_level, SolvableId conflicting_solvable,
                                                    ClauseId clause_id) {
        std::unordered_set<SolvableId> seen;
        uint32_t causes_at_current_level = 0;
        std::vector<Literal> learnt;
        uint32_t back_track_to = 0;

        bool s_value;
        std::vector<ClauseId> learnt_why;
        bool first_iteration = true;
        while (true) {
            learnt_why.push_back(clause_id);

            auto &clause = clauses_[clause_id];
            clause.visit_literals(learnt_clauses_, cache.version_set_to_sorted_candidates,
                                  [&first_iteration, &seen, &current_level, &causes_at_current_level, &back_track_to, &conflicting_solvable,
                                          &learnt, this](const Literal &literal) {
                                      if (!first_iteration && literal.solvable_id == conflicting_solvable) {
                                          return;
                                      }

                                      if (seen.find(literal.solvable_id) != seen.end()) {
                                          return;
                                      }

                                      auto decision_level = decision_tracker_.get_level(literal.solvable_id);
                                      if (decision_level == current_level) {
                                          causes_at_current_level++;
                                      } else if (current_level > 1) {
                                          auto learnt_literal = Literal(literal.solvable_id,
                                                                        !decision_tracker_.assigned_value(
                                                                                literal.solvable_id).value());
                                          learnt.push_back(learnt_literal);
                                          back_track_to = std::max(back_track_to, decision_level);
                                      } else {
                                          throw std::runtime_error("Unreachable");
                                      }
                                  });

            first_iteration = false;

            while (true) {
                auto [last_decision, last_decision_level] = decision_tracker_.undo_last();
                conflicting_solvable = last_decision.solvable_id;
                s_value = last_decision.value;
                clause_id = last_decision.derived_from;

                current_level = last_decision_level;

                // We are interested in the first literal we come across that caused the conflicting
                // assignment
                if (seen.find(last_decision.solvable_id) != seen.end()) {
                    break;
                }
            }

            causes_at_current_level--;
            if (causes_at_current_level == 0) {
                break;
            }
        }


        auto last_literal = Literal(conflicting_solvable, s_value);
        learnt.push_back(last_literal);

        auto learnt_clause_id = learnt_clauses_.alloc(learnt);
        learnt_why_.insert({learnt_clause_id, learnt_why});

        auto new_clause_id = clauses_.alloc(ClauseState::learnt(learnt_clause_id, learnt));
        learnt_clause_ids_.push_back(new_clause_id);

        auto &clause = clauses_[new_clause_id];
        if (clause.has_watches()) {
            watches_.start_watching(clause, new_clause_id);
        }

        tracing::debug("├─ Learnt disjunction:");
        for (auto lit: learnt) {
            tracing::debug(
                    "│  - {}{}"
//                        if lit.negate { "NOT " } else { "" },
//                        lit.solvable_id.display(&self.pool)
            );
        }

        // Should revert at most to the root level
        auto target_level = back_track_to < 1 ? 1 : back_track_to;
        decision_tracker_.undo_until(target_level);
        return {target_level, new_clause_id, last_literal};

    }
};
