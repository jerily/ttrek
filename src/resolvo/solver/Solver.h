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
#include "UnsolvableOrCancelled.h"
#include "PropagationError.h"
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

template<typename VS, typename N>
class DependencyProvider;

template<typename VS, typename N, typename D, typename RT>
class SolverCache;

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
        std::vector<SolvableId> package_candidates;
    };
}

using TaskResultVariant = std::variant<TaskResult::Dependencies, TaskResult::SortedCandidates, TaskResult::NonMatchingCandidates, TaskResult::Candidates>;

// Drives the SAT solving process
template<typename VS, typename N, typename D, typename RT = NowOrNeverRuntime>
class Solver {
private:
    std::vector<std::tuple<SolvableId, VersionSetId, ClauseId>> requires_clauses_;
    WatchMap watches_;

    std::vector<std::tuple<SolvableId, ClauseId>> negative_assertions_;

    Arena<LearntClauseId, std::vector<Literal>> learnt_clauses_;
    std::map<LearntClauseId, std::vector<ClauseId>> learnt_why_;
    std::vector<ClauseId> learnt_clause_ids_;

    std::unordered_set<NameId> clauses_added_for_package_;
    std::unordered_set<SolvableId> clauses_added_for_solvable_;

    DecisionTracker decision_tracker_;

// The version sets that must be installed as part of the solution.
    std::vector<VersionSetId> root_requirements_;

public:

    // The Pool used by the solver
    Pool<VS, N> pool;
    RT async_runtime;
    SolverCache<VS, N, D, RT> cache;
    Arena<ClauseId, ClauseState> clauses_;

    // Constructor
    Solver(D provider) :
            pool(provider.pool()), async_runtime(NowOrNeverRuntime()), cache(provider) {}

    auto solve(std::vector<VersionSetId> root_requirements) {
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
        run_sat();

        std::vector<SolvableId> steps;
        for (auto &d: decision_tracker_.get_stack()) {
            if (d.value && d.solvable_id != SolvableId::root()) {
                steps.push_back(d.solvable_id);
            }
            // Ignore things that are set to false
        }

        return steps;
    }

    // Adds clauses for a solvable. These clauses include requirements and constrains on other
    // solvables.
    //
    // Returns the added clauses, and an additional list with conflicting clauses (if any).
    //
    // If the provider has requested the solving process to be cancelled, the cancellation value
    // will be returned as an `Err(...)`.
    std::pair<AddClauseOutput<SolvableId, VersionSetId, ClauseId>, std::optional<std::any>> add_clauses_for_solvables(
            const std::vector<SolvableId> &solvable_ids) {

        auto output = AddClauseOutput<SolvableId, VersionSetId, ClauseId>();

        // Mark the initial seen solvables as seen
        std::vector<SolvableId> pending_solvables;
        auto clauses_added_for_solvable = clauses_added_for_solvable_;
        for (auto &solvable_id: solvable_ids) {
            if (clauses_added_for_solvable_.find(solvable_id) == clauses_added_for_solvable_.end()) {
                pending_solvables.push_back(solvable_id);
            }
        }


        std::unordered_set<SolvableId, SolvableId::Hash> seen(pending_solvables.cbegin(), pending_solvables.cend());
        std::vector<TaskResultVariant> pending_futures;
        while (true) {
            // Iterate over all pending solvables and request their dependencies.
            for (SolvableId solvable_id: pending_solvables) {
                // Get the solvable information and request its requirements and constraints
                auto solvable = pool.resolve_internal_solvable(solvable_id);

                //tracing::trace!(
                //                    "┝━ adding clauses for dependencies of {}",
                //                    solvable.display(&self.pool)
                //                );

                auto solvable_inner_type = solvable.get_inner().get_type();
                if (solvable_inner_type == SolvableInnerType::Root) {
                    auto known_dependencies = KnownDependencies{root_requirements_, std::vector<VersionSetId>()};
                    auto task_result = TaskResult::Dependencies{solvable_id, Dependencies::Known{known_dependencies}};
                    auto get_dependencies_fut = task_result;
                    pending_futures.emplace_back(get_dependencies_fut);
                } else if (solvable_inner_type == SolvableInnerType::Package) {
                    auto deps = cache.get_or_cache_dependencies(solvable_id);
                    auto task_result = TaskResult::Dependencies{solvable_id, deps};
                    auto get_dependencies_fut = task_result;
                    pending_futures.emplace_back(get_dependencies_fut);
                }

            }

            if (pending_futures.empty()) {
                // No more pending results
                break;
            }

            auto &result = pending_futures.front();  // todo: pending_futures.next()

            auto continue_p = std::visit([this, &output, &pending_futures, &pending_solvables, &seen](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;

                if constexpr (std::is_same_v<T, TaskResult::Dependencies>) {
                    auto task_result = std::any_cast<TaskResult::Dependencies>(arg);
                    auto solvable_id = task_result.solvable_id;
                    auto dependencies = task_result.dependencies;

                    auto solvable = pool.resolve_internal_solvable(solvable_id);

                    // tracing::trace!(
                    //                        "dependencies available for {}",
                    //                        solvable.display(&self.pool)
                    //                    );

                    auto continue_inner_p = std::visit([this, &output, &pending_futures](auto &&arg_inner) {
                        using T_INNER = std::decay_t<decltype(arg_inner)>;

                        auto requirements = std::vector<VersionSetId>();
                        auto constrains = std::vector<VersionSetId>();
                        if constexpr (std::is_same_v<T_INNER, Dependencies::Known>) {
                            auto known_dependencies = std::any_cast<Dependencies::Known>(
                                    dependencies).known_dependencies;
                            requirements = known_dependencies.requirements;
                            constrains = known_dependencies.constrains;
                        } else if constexpr (std::is_same_v<T_INNER, Dependencies::Unknown>) {
                            auto reason = std::any_cast<Dependencies::Unknown>(dependencies).reason;

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


                        std::vector<VersionSetId> chain = requirements;
                        chain.emplace_back(constrains);
                        for (auto &version_set_id: chain) {
                            auto dependency_name = pool.resolve_version_set_package_name(version_set_id);

                            if (clauses_added_for_solvable_.insert(dependency_name).second) {
                                // tracing::trace!(
                                //                                "┝━ adding clauses for package '{}'",
                                //                                        self.pool.resolve_package_name(dependency_name)
                                //                        );

                                auto package_candidates = cache.get_or_cache_candidates(dependency_name);
                                pending_futures.emplace_back(
                                        TaskResult::Candidates{dependency_name, package_candidates});
                            }
                        }

                        for (VersionSetId version_set_id: requirements) {
                            auto candidates = cache.get_or_cache_sorted_candidates(version_set_id);
                            pending_futures.emplace_back(
                                    TaskResult::SortedCandidates{solvable_id, version_set_id, candidates});
                        }

                        for (VersionSetId version_set_id: constrains) {
                            auto non_matching_candidates = cache.get_or_cache_non_matching_candidates(version_set_id);
                            pending_futures.emplace_back(TaskResult::NonMatchingCandidates{solvable_id, version_set_id,
                                                                                           non_matching_candidates});
                        }

                        return false;
                    }, dependencies);

                    if (continue_inner_p) {
                        return true;  // continue
                    }

                } else if constexpr (std::is_same_v<T, TaskResult::Candidates>) {
                    auto task_result = std::any_cast<TaskResult::Candidates>(arg);
                    auto name_id = task_result.name_id;
                    auto package_candidates = task_result.package_candidates;
                    auto solvable = pool.resolve_package_name(name_id);
                    // tracing::trace!("package candidates available for {}", solvable);

                    auto locked_solvable_id = package_candidates.locked;
                    auto candidates = package_candidates.candidates;

                    // Check the assumption that no decision has been made about any of the solvables.
                    for (auto &candidate: candidates) {
                        // "a decision has been made about a candidate of a package that was not properly added yet."
                        assert(decision_tracker_.assigned_value(candidate).has_value());
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

                    auto version_set_name = pool.resolve_package_name(
                            pool.resolve_version_set_package_name(version_set_id));

                    auto version_set = pool.resolve_version_set(version_set_id);

                    //tracing::trace!(
                    //                        "sorted candidates available for {} {}",
                    //                        version_set_name,
                    //                        version_set
                    //                    );

                    // Queue requesting the dependencies of the candidates as well if they are cheaply
                    // available from the dependency provider.

                    for (auto &candidate: candidates) {
                        if (seen.insert(candidate) && cache.are_dependencies_available_for(candidate) &&
                            clauses_added_for_solvable_.insert(candidate)) {
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

                    auto version_set_name = pool.resolve_package_name(
                            pool.resolve_version_set_package_name(version_set_id));
                    auto version_set = pool.resolve_version_set(version_set_id);

                    //tracing::trace!(
                    //                        "non matching candidates available for {} {}",
                    //                        version_set_name,
                    //                        version_set
                    //                    );

                    // Add forbidden clauses for the candidates
                    for (auto &forbidden_candidate: non_matching_candidates) {
                        auto [constrains_clause, conflict] = clauses_.alloc(
                                ClauseState::constrains(solvable_id, forbidden_candidate, version_set_id,
                                                        decision_tracker_));
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
                continue;
            }

        }

        return {output, std::nullopt};

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

    std::optional<UnsolvableOrCancelled> run_sat() {
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

                //tracing::info!(
                //                    "╤══ install {} at level {level}",
                //                    SolvableId::root().display(&self.pool)
                //                );

                decision_tracker_.try_add_decision(Decision(SolvableId::root(), true, ClauseId::install_root()), level);

                auto output = add_clauses_for_solvables({SolvableId::root()});

                auto optional_clause_id = process_add_clause_output(output);
                if (optional_clause_id.has_value()) {
                    return Unsolvable(analyze_unsolvable(optional_clause_id.value()));
                }
            }

            // Propagate decisions from assignments above
            auto propagate_result = propagate(level);

            // TODO: Handle propagation errors

            // Enter the solver loop, return immediately if no new assignments have been made.
            level = resolve_dependencies(level);

            // We have a partial solution. E.g. there is a solution that satisfies all the clauses
            // that have been added so far.

            // Determine which solvables are part of the solution for which we did not yet get any
            // dependencies. If we find any such solvable it means we did not arrive at the full
            // solution yet.

            // let new_solvables: Vec<_> = self
            //                .decision_tracker
            //                .stack()
            //                // Filter only decisions that led to a positive assignment
            //                .filter(|d| d.value)
            //                // Select solvables for which we do not yet have dependencies
            //                .filter(|d| {
            //                    !self
            //                        .clauses_added_for_solvable
            //                        .borrow()
            //                        .contains(&d.solvable_id)
            //                })
            //                .map(|d| (d.solvable_id, d.derived_from))
            //                .collect();
            //
            // from let in c++ it should be:

            std::vector<std::tuple<SolvableId, ClauseId>> new_solvables;
            for (auto &d: decision_tracker_.get_stack()) {
                if (d.value && clauses_added_for_solvable_.find(d.solvable_id) == clauses_added_for_solvable_.end()) {
                    new_solvables.emplace_back(d.solvable_id, d.derived_from);
                }
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

            // Concurrently get the solvable's clauses
            auto new_solvable_ids = std::vector<SolvableId>();
            for (auto &[solvable_id, derived_from]: new_solvables) {
                new_solvable_ids.push_back(solvable_id);
            }
            auto output = add_clauses_for_solvables(new_solvable_ids);

            // Serially process the outputs, to reduce the need for synchronization
            // for &clause_id in &output.conflicting_clauses {
            //                tracing::debug!("├─ added clause {clause:?} introduces a conflict which invalidates the partial solution",
            //                        clause=self.clauses.borrow()[clause_id].debug(&self.pool));
            //            }

            auto optional_conflicting_clause_id = process_add_clause_output(output);
            if (optional_conflicting_clause_id.has_value()) {
                decision_tracker_.clear();
                level = 0;
            }
        }
    }

    std::optional<ClauseId> process_add_clause_output(AddClauseOutput<SolvableId, VersionSetId, ClauseId> output) {
        for (auto &[solvable_id, version_set_id, clause_id]: output.new_requires_clauses) {
            requires_clauses_.emplace_back(solvable_id, version_set_id, clause_id);
        }
        for (auto &clause_id: output.negative_assertions) {
            negative_assertions_.push_back({clause_id});
        }

        for (auto &clause_id: output.clauses_to_watch) {
            assert(clauses_[clause_id].has_watches());
            watches_.start_watching(clauses_[clause_id], clause_id);
        }

        if (!output.conflicting_clauses.empty()) {
            return output.conflicting_clauses[0];
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

    std::pair<uint32_t, std::optional<UnsolvableOrCancelled>> resolve_dependencies(uint32_t level) {
        while (true) {
            auto decision = decide();
            if (!decision.has_value()) {
                break;
            }

            auto [candidate, required_by, clause_id] = decision.value();

            auto [new_level, optional_unsolvable] = set_propagate_learn(level, candidate, required_by, clause_id);
            if (optional_unsolvable.has_value()) {
                return {new_level, optional_unsolvable};
            }

            level = new_level;
        }

        return {level, std::nullopt};
    }

    // Pick a solvable that we are going to assign true. This function uses a heuristic to
    // determine to best decision to make. The function selects the requirement that has the least
    // amount of working available candidates and selects the best candidate from that list. This
    // ensures that if there are conflicts they are delt with as early as possible.

    std::optional<std::tuple<SolvableId, SolvableId, ClauseId>> decide() {
        std::pair<uint32_t, std::optional<std::tuple<SolvableId, SolvableId, ClauseId>>> best_decision;
        for (auto &[solvable_id, deps, clause_id]: requires_clauses_) {
            if (decision_tracker_.assigned_value(solvable_id) != std::optional<bool>(true)) {
                continue;
            }

            auto candidates = cache.version_set_to_sorted_candidates[deps];

            std::optional<SolvableId> first_selectable_candidate;
            int selectable_candidates = 0;
            for (auto &candidate: candidates) {
                auto assigned_value = decision_tracker_.assigned_value(candidate);
                if (assigned_value.has_value()) {
                    if (assigned_value.value()) {
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
//        auto [candidate, _solvable_id, clause_id] = the_decision.value();
        // tracing::info!(
        //                "deciding to assign {}, ({:?}, {} possible candidates)",
        //                candidate.display(&self.pool),
        //                self.clauses.borrow()[clause_id].debug(&self.pool),
        //                count,
        //            );

        return the_decision;
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
    //fn set_propagate_learn(
    //    &mut self,
    //            mut level: u32,
    //            solvable: SolvableId,
    //            required_by: SolvableId,
    //            clause_id: ClauseId,
    //    ) -> Result<u32, UnsolvableOrCancelled> {
    //        level += 1;
    //
    //        tracing::info!(
    //                "╤══ Install {} at level {level} (required by {})",
    //                        solvable.display(&self.pool),
    //                        required_by.display(&self.pool),
    //        );
    //
    //        // Add the decision to the tracker
    //        self.decision_tracker
    //                .try_add_decision(Decision::new(solvable, true, clause_id), level)
    //        .expect("bug: solvable was already decided!");
    //
    //        self.propagate_and_learn(level)
    //    }
    std::pair<uint32_t, std::optional<UnsolvableOrCancelled>>
    set_propagate_learn(uint32_t level, const SolvableId &solvable, const SolvableId &required_by,
                        const ClauseId &clause_id) {
        level += 1;

        //        tracing::info!(
        //                "╤══ Install {} at level {level} (required by {})",
        //                        solvable.display(&self.pool),
        //                        required_by.display(&self.pool),
        //        );
        //

        // Add the decision to the tracker
        decision_tracker_.try_add_decision(Decision(solvable, true, clause_id), level);

        return propagate_and_learn(level);
    }

    // fn propagate_and_learn(&mut self, mut level: u32) -> Result<u32, UnsolvableOrCancelled> {
    //        loop {
    //            match self.propagate(level) {
    //                Ok(()) => {
    //                    // Propagation completed
    //                    tracing::debug!("╘══ Propagation completed");
    //                    return Ok(level);
    //                }
    //                Err(PropagationError::Cancelled(value)) => {
    //                    // Propagation cancelled
    //                    tracing::debug!("╘══ Propagation cancelled");
    //                    return Err(UnsolvableOrCancelled::Cancelled(value));
    //                }
    //                Err(PropagationError::Conflict(
    //                    conflicting_solvable,
    //                    attempted_value,
    //                    conflicting_clause,
    //                )) => {
    //                    level = self.learn_from_conflict(
    //                        level,
    //                        conflicting_solvable,
    //                        attempted_value,
    //                        conflicting_clause,
    //                    )?;
    //                }
    //            }
    //        }
    //    }

    std::pair<uint32_t, std::optional<UnsolvableOrCancelled>> propagate_and_learn(uint32_t level) {
        while (true) {
            auto propagate_result = propagate(level);
            if (!propagate_result.has_value()) {
                return {level, std::nullopt};
            }

            // TODO: HERE
            auto [conflicting_solvable, attempted_value, conflicting_clause] = propagate_result.value();
            auto new_level = learn_from_conflict(level, conflicting_solvable, attempted_value, conflicting_clause);
            if (new_level.has_value()) {
                level = new_level.value();
            } else {
                return {level, Cancelled(nullptr)};
            }
        }
    }

    std::pair<uint32_t, std::optional<Problem>>
    learn_from_conflict(uint32_t level, const SolvableId &conflicting_solvable,
                        bool attempted_value, const ClauseId &conflicting_clause) {
        //        {
        //            tracing::info!(
        //                "├─ Propagation conflicted: could not set {solvable} to {attempted_value}",
        //                solvable = conflicting_solvable.display(&self.pool)
        //            );
        //            tracing::info!(
        //                "│  During unit propagation for clause: {:?}",
        //                self.clauses.borrow()[conflicting_clause].debug(&self.pool)
        //            );
        //
        //            tracing::info!(
        //                "│  Previously decided value: {}. Derived from: {:?}",
        //                !attempted_value,
        //                self.clauses.borrow()[self
        //                    .decision_tracker
        //                    .find_clause_for_assignment(conflicting_solvable)
        //                    .unwrap()]
        //                .debug(&self.pool),
        //            );
        //        }

        if (level == 1) {
            //            tracing::info!("╘══ UNSOLVABLE");
            //            for decision in self.decision_tracker.stack() {
            //                let clause = &self.clauses.borrow()[decision.derived_from];
            //                let level = self.decision_tracker.level(decision.solvable_id);
            //                let action = if decision.value { "install" } else { "forbid" };
            //
            //                if let Clause::ForbidMultipleInstances(..) = clause.kind {
            //                    // Skip forbids clauses, to reduce noise
            //                    continue;
            //                }
            //
            //                tracing::info!(
            //                    "* ({level}) {action} {}. Reason: {:?}",
            //                    decision.solvable_id.display(&self.pool),
            //                    clause.debug(&self.pool),
            //                );
            //            }
            //
            //            return Err(self.analyze_unsolvable(conflicting_clause));
        }

        auto [new_level, learned_clause_id, literal] = analyze(level, conflicting_solvable, conflicting_clause);
        level = new_level;

        //        tracing::debug!("├─ Backtracked to level {level}");

        // Optimization: propagate right now, since we know that the clause is a unit clause
        auto decision = literal.satisfying_value();
        decision_tracker_.try_add_decision(Decision(literal.solvable_id, decision, learned_clause_id), level);
        //        tracing::debug!(
        //            "├─ Propagate after learn: {} = {decision}",
        //            literal.solvable_id.display(&self.pool)
        //        );
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

    std::optional<PropagationError> propagate(uint32_t level) {

        auto optional_value = cache.provider.should_cancel_with_value();
        if (optional_value.has_value()) {
            return PropagationErrorCancelled(optional_value.value_());
        }

        // Negative assertions derived from other rules (assertions are clauses that consist of a
        // single literal, and therefore do not have watches)
        for (auto &[solvable_id, clause_id]: negative_assertions_) {
            bool value = false;
            auto decided = decision_tracker_.try_add_decision(Decision(solvable_id, value, clause_id), level);
            if (!decided.has_value()) {
                return ConflictPropagationError(solvable_id, value, clause_id);
            }

            // if decided {
            //            tracing::trace!(
            //                        "├─ Propagate assertion {} = {}",
            //                        solvable_id.display(&self.pool),
            //                        value
            //                        );
            // }
        }

        // Assertions derived from learnt rules
        for (auto &learn_clause_id: learnt_clause_ids_) {
            auto clause_id = learn_clause_id;
            auto clause = clauses_[clause_id];

            if (clause.get_kind().get_type() != ClauseType::Learnt) {
                // unreachable
            }

            auto learnt_index = static_cast<const LearntClause *>(&(clause.get_kind()))->learnt_clause_id_;
            auto literals = learnt_clauses_[learnt_index];
            if (literals.size() > 1) {
                continue;
            }

            auto literal = literals[0];
            auto decision = literal.satisfying_value();

            auto decided = decision_tracker_.try_add_decision(Decision(literal.solvable_id, decision, clause_id),
                                                              level);
            if (!decided.has_value()) {
                return ConflictPropagationError(literal.solvable_id, decision, clause_id);
            }

            // if decided {
            //            tracing::trace!(
            //                        "├─ Propagate assertion {} = {}",
            //                        literal.solvable_id.display(&self.pool),
            //                        decision
            //                        );
            // }
        }

        // Watched solvables
        while (true) {
            auto decision = decision_tracker_.next_unpropagated();
            if (!decision.has_value()) {
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
                            return ConflictPropagationError(remaining_watch.solvable_id, true, this_clause_id);
                        }

                        //if decided {
                        //                            match clause.kind {
                        //                                // Skip logging for ForbidMultipleInstances, which is so noisy
                        //                                Clause::ForbidMultipleInstances(..) => {}
                        //                                _ => {
                        //                                    tracing::debug!(
                        //                                        "├─ Propagate {} = {}. {:?}",
                        //                                        remaining_watch.solvable_id.display(&self.cache.pool()),
                        //                                        remaining_watch.satisfying_value(),
                        //                                        clause.debug(&self.cache.pool()),
                        //                                    );
                        //                                }
                        //                            }
                        //                        }
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
        if (clause.get_kind().get_type() == ClauseType::Learnt) {
            auto learnt_clause_id = static_cast<const LearntClause *>(&(clause.get_kind()))->learnt_clause_id_;
            if (seen.find(clause_id) == seen.end()) {
                return;
            }

            for (auto &cause: learnt_why.at(learnt_clause_id)) {
                analyze_unsolvable_clause(clauses, learnt_why, cause, problem, seen);
            }
        } else {
            problem.add_clause(clause_id);
        }
    }

    // Create a [`Problem`] based on the id of the clause that triggered an unrecoverable conflict
    Problem analyze_unsolvable(const ClauseId &clause_id) {
        auto last_decision = decision_tracker_.get_stack().back();
        auto highest_level = decision_tracker_.get_level(last_decision.solvable_id);
        assert(highest_level == 1);

        auto problem = Problem();

        std::unordered_set<SolvableId, SolvableId::Hash> involved;
        auto &clause = clauses_[clause_id];
        clause.get_kind().visit_literals(learnt_clauses_, cache.version_set_to_sorted_candidates,
                                         [&involved](const Literal &literal) {
                                             involved.insert(literal.solvable_id);
                                         });

        std::unordered_set<ClauseId, ClauseId::Hash> seen;
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

            why_clause.get_kind().visit_literals(learnt_clauses_, cache.version_set_to_sorted_candidates,
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
        std::unordered_set<SolvableId, SolvableId::Hash> seen;
        uint32_t causes_at_current_level = 0;
        std::vector<Literal> learnt;
        uint32_t back_track_to = 0;

        bool s_value;
        std::vector<ClauseId> learnt_why;
        bool first_iteration = true;
        while (true) {
            learnt_why.push_back(clause_id);

            auto &clause = clauses_[clause_id];
            clause.get_kind().visit_literals(learnt_clauses_, cache.version_set_to_sorted_candidates,
                                             [&first_iteration, &seen, &current_level, &causes_at_current_level, &back_track_to, &conflicting_solvable,
                                                     &s_value, &clause_id, &learnt, this](const Literal &literal) {
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

        auto learnt_id = learnt_clauses_.alloc(learnt);
        learnt_why_.insert({learnt_id, learnt_why});

        clause_id = clauses_.alloc(Clause::learnt(learnt_id, learnt));
        learnt_clause_ids_.push_back(clause_id);

        auto &clause = clauses_[clause_id];
        if (clause.has_watches()) {
            watches_.start_watching(clause, clause_id);
        }

        //        tracing::debug!("├─ Learnt disjunction:",);
        //        for lit in learnt {
        //                tracing::debug!(
        //                "│  - {}{}",
        //                if lit.negate { "NOT " } else { "" },
        //                lit.solvable_id.display(&self.pool)
        //                );
        //        }

        // Should revert at most to the root level
        auto target_level = back_track_to < 1 ? 1 : back_track_to;
        decision_tracker_.undo_until(target_level);
        return {target_level, clause_id, last_literal};

    }
};
