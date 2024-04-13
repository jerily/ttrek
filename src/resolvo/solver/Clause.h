#ifndef CLAUSE_H
#define CLAUSE_H

#include <cassert>
#include <utility>
#include <vector>
#include <string>
#include <map>
#include <algorithm>

#include <cassert>
#include <vector>
#include <optional>
#include <unordered_map>
#include <variant>
#include <any>

#include "../internal/SolvableId.h"
#include "../internal/VersionSetId.h"
#include "../internal/LearntClauseId.h"
#include "../internal/StringId.h"
#include "../internal/Arena.h"
#include "../internal/ClauseId.h"
#include "../internal/FrozenCopyMap.h"
#include "DecisionTracker.h"
#include "DecisionMap.h"
#include "../internal/FrozenMap.h"


struct Literal {
public:
    SolvableId solvable_id;
    bool negate;

    explicit Literal(SolvableId id, bool neg) : solvable_id(std::move(id)), negate(neg) {}

    // Returns the value that would make the literal evaluate to true if assigned to the literal's solvable
    bool satisfying_value() const {
        return !negate;
    }

    // Evaluates the literal, or returns std::nullopt if no value has been assigned to the solvable
    std::optional<bool> eval(const DecisionMap &decision_map) const {
        auto optional_value = decision_map.value(solvable_id);
        if (optional_value.has_value()) {
            return eval_inner(optional_value.value());
        }
        return std::nullopt;
    }

private:
    bool eval_inner(bool solvable_value) const {
        return negate == !solvable_value;
    }
};

using VisitFunction = std::function<void(Literal)>;

namespace Clause {

    struct InstallRoot {
    };

    struct Requires {
        SolvableId parent;
        VersionSetId requirement;
    };

    struct ForbidMultipleInstances {
        SolvableId candidate;
        SolvableId constrained_candidate;
    };

    struct Constrains {
        SolvableId parent;
        SolvableId forbidden_solvable;
        VersionSetId via;
    };

    struct Lock {
        SolvableId locked_candidate;
        SolvableId other_candidate;
    };

    struct Learnt {
        LearntClauseId learnt_clause_id;
    };

    struct Excluded {
        SolvableId candidate;
        StringId reason;
    };

    // Factory methods for creating different kinds of clauses

    static std::tuple<Requires, std::optional<std::array<SolvableId, 2>>, bool> requires(
            const SolvableId &parent,
            const VersionSetId &requirement,
            const std::vector<SolvableId> &candidates,
            const DecisionTracker &decision_tracker
    );

    static std::tuple<Constrains, std::optional<std::array<SolvableId, 2>>, bool> constrains(
            const SolvableId &parent,
            const SolvableId &forbidden_solvable,
            const VersionSetId &via,
            const DecisionTracker &decision_tracker
    );

    static std::pair<ForbidMultipleInstances, std::optional<std::array<SolvableId, 2>>> forbid_multiple(
            const SolvableId &candidate,
            const SolvableId &constrained_candidate
    );

    static std::pair<InstallRoot, std::optional<std::array<SolvableId, 2>>> root();

    static std::pair<Excluded, std::optional<std::array<SolvableId, 2>>> exclude(
            const SolvableId &candidate,
            const StringId &reason
    );

    static std::pair<Lock, std::optional<std::array<SolvableId, 2>>> lock(
            const SolvableId &locked_candidate,
            const SolvableId &other_candidate
    );

    static std::pair<Learnt, std::optional<std::array<SolvableId, 2>>> learnt(
            const LearntClauseId &learnt_clause_id,
            const std::vector<Literal> &literals
    );

};

using ClauseVariant = std::variant<Clause::InstallRoot, Clause::Requires, Clause::ForbidMultipleInstances, Clause::Constrains, Clause::Lock, Clause::Learnt, Clause::Excluded>;

std::tuple<Clause::Requires, std::optional<std::array<SolvableId, 2>>, bool> Clause::requires(
        const SolvableId &parent,
        const VersionSetId &requirement,
        const std::vector<SolvableId> &candidates,
        const DecisionTracker &decision_tracker
) {
    // It only makes sense to introduce a requires clause when the parent solvable is undecided
    // or going to be installed
    assert(decision_tracker.assigned_value(parent) != false);

    auto kind = Clause::Requires{parent, requirement};
    if (candidates.empty()) {
        return std::make_tuple(kind, std::nullopt, false);
    }

    auto watched_candidate = std::find_if(candidates.begin(), candidates.end(), [&](const SolvableId &c) {
        return decision_tracker.assigned_value(c) != false;
    });

    std::optional<std::array<SolvableId, 2>> watches;

    bool conflict;
    if (watched_candidate != candidates.end()) {
        watches = {parent, *watched_candidate};
        conflict = false;
    } else {
        watches = {parent, candidates[0]};
        conflict = true;
    }
    return std::make_tuple(kind, watches, conflict);

}


std::tuple<Clause::Constrains, std::optional<std::array<SolvableId, 2>>, bool> Clause::constrains(
        const SolvableId &parent,
        const SolvableId &forbidden_solvable,
        const VersionSetId &via,
        const DecisionTracker &decision_tracker
) {
    assert(decision_tracker.assigned_value(parent) != false);

    bool conflict = decision_tracker.assigned_value(forbidden_solvable) == true;
    auto kind = Clause::Constrains{parent, forbidden_solvable, via};

    // return clause, (parent, forbidden_solvable), conflict
    return std::make_tuple(kind, std::array<SolvableId, 2>{parent, forbidden_solvable}, conflict);
}


std::pair<Clause::ForbidMultipleInstances, std::optional<std::array<SolvableId, 2>>> Clause::forbid_multiple(
        const SolvableId &candidate,
        const SolvableId &constrained_candidate
) {
    auto kind = Clause::ForbidMultipleInstances{candidate, constrained_candidate};
    return std::make_pair(kind, std::array<SolvableId, 2>{candidate, constrained_candidate});
}

std::pair<Clause::InstallRoot, std::optional<std::array<SolvableId, 2>>> Clause::root() {
    auto kind = Clause::InstallRoot{};
    return std::make_pair(kind, std::nullopt);
}


std::pair<Clause::Excluded, std::optional<std::array<SolvableId, 2>>> Clause::exclude(
        const SolvableId &candidate,
        const StringId &reason
) {
    auto kind = Clause::Excluded{candidate, reason};
    return std::make_pair(kind, std::nullopt);
}

std::pair<Clause::Lock, std::optional<std::array<SolvableId, 2>>> Clause::lock(
        const SolvableId &locked_candidate,
        const SolvableId &other_candidate
) {
    auto kind = Clause::Lock{locked_candidate, other_candidate};
    return std::make_pair(kind, std::array<SolvableId, 2>{SolvableId::root(), other_candidate});
}

std::pair<Clause::Learnt, std::optional<std::array<SolvableId, 2>>> Clause::learnt(
        const LearntClauseId &learnt_clause_id,
        const std::vector<Literal> &literals
) {
    assert(!literals.empty());

    auto kind = Clause::Learnt{learnt_clause_id};

    std::optional<std::array<SolvableId, 2>> watches;

    if (literals.size() == 1) {
        return std::make_pair(kind, std::nullopt);
    } else {
        watches = {literals.front().solvable_id, literals.back().solvable_id};
    }

    return std::make_pair(kind, watches);
}

class ClauseState {
public:
    ClauseState(
            const std::array<SolvableId, 2> &watched_literals,
            const std::array<ClauseId, 2> &next_watches,
            ClauseVariant kind
    ) : watched_literals_(watched_literals), next_watches_(next_watches), kind_(std::move(kind)) {}

    static ClauseState root() {
        auto [kind, watched_literals] = Clause::root();
        return from_kind_and_initial_watches(kind, watched_literals);
    }

    static std::pair<ClauseState, bool> requires(
            const SolvableId &candidate,
            const VersionSetId &requirement,
            const std::vector<SolvableId> &matching_candidates,
            const DecisionTracker &decision_tracker
    ) {
        auto [kind, watched_literals, conflict] = Clause::requires(
                candidate,
                requirement,
                matching_candidates,
                decision_tracker
        );

        return std::make_pair(from_kind_and_initial_watches(kind, watched_literals), conflict);
    }

    static std::pair<ClauseState, bool> constrains(
            const SolvableId &candidate,
            const SolvableId &constrained_package,
            const VersionSetId &requirement,
            const DecisionTracker &decision_tracker
    ) {
        auto [kind, watched_literals, conflict] = Clause::constrains(
                candidate,
                constrained_package,
                requirement,
                decision_tracker
        );

        return std::make_pair(from_kind_and_initial_watches(kind, watched_literals), conflict);
    }

    static ClauseState lock(const SolvableId &locked_candidate, const SolvableId &other_candidate) {
        auto [kind, watched_literals] = Clause::lock(locked_candidate, other_candidate);
        return from_kind_and_initial_watches(kind, watched_literals);
    }

    static ClauseState forbid_multiple(const SolvableId &candidate, const SolvableId &other_candidate) {
        auto [kind, watched_literals] = Clause::forbid_multiple(candidate, other_candidate);
        return from_kind_and_initial_watches(kind, watched_literals);
    }

    static ClauseState learnt(const LearntClauseId &learnt_clause_id, const std::vector<Literal> &literals) {
        auto [kind, watched_literals] = Clause::learnt(learnt_clause_id, literals);
        return from_kind_and_initial_watches(kind, watched_literals);
    }

    static ClauseState exclude(const SolvableId &candidate, const StringId &reason) {
        auto [kind, watched_literals] = Clause::exclude(candidate, reason);
        return from_kind_and_initial_watches(kind, watched_literals);
    }

    void link_to_clause(size_t watch_index, const ClauseId &clause_id) {
        next_watches_[watch_index] = clause_id;
    }

    ClauseId get_linked_clause(size_t watch_index) const {
        return next_watches_[watch_index];
    }

    void
    unlink_clause(const ClauseState *linked_clause, SolvableId watched_solvable, size_t linked_clause_watch_index) {
        if (watched_literals_[0] == watched_solvable) {
            next_watches_[0] = linked_clause->next_watches_[linked_clause_watch_index];
        } else {
            assert(watched_literals_[1] == watched_solvable);
            next_watches_[1] = linked_clause->next_watches_[linked_clause_watch_index];
        }
    }

    ClauseId next_watched_clause(const SolvableId &solvable_id) const {
        if (solvable_id == watched_literals_[0]) {
            return next_watches_[0];
        } else {
            assert(watched_literals_[1] == solvable_id);
            return next_watches_[1];
        }
    }

    std::optional<std::pair<std::array<Literal, 2>, size_t>> watch_turned_false(
            const SolvableId &solvable_id,
            const DecisionMap &decision_map,
            const Arena<LearntClauseId, std::vector<Literal>> &learnt_clauses
    ) const {
        assert(watched_literals_[0] == solvable_id || watched_literals_[1] == solvable_id);

        auto literals = watched_literals(learnt_clauses);

        if (solvable_id == literals[0].solvable_id && !literals[0].eval(decision_map).value()) {
            return std::make_pair(literals, 0);
        } else if (solvable_id == literals[1].solvable_id && !literals[1].eval(decision_map).value()) {
            return std::make_pair(literals, 1);
        } else {
            return std::nullopt;
        }
    }

    std::array<Literal, 2> watched_literals(const Arena<LearntClauseId, std::vector<Literal>> &learnt_clauses) const {
        auto literals = [&](bool op1, bool op2) {
            return std::array<Literal, 2>{
                    Literal(watched_literals_[0], !op1),
                    Literal(watched_literals_[1], !op2)
            };
        };

        return std::visit([&](auto &arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Clause::InstallRoot>) {
                assert(false);
                // unreachable
                return std::array<Literal, 2>{Literal(SolvableId::null(), false), Literal(SolvableId::null(), false)};
            } else if constexpr (std::is_same_v<T, Clause::Excluded>) {
                assert(false);
                // unreachable
                return std::array<Literal, 2>{Literal(SolvableId::null(), false), Literal(SolvableId::null(), false)};
            } else if constexpr (std::is_same_v<T, Clause::Learnt>) {
                auto clause_variant = std::any_cast<Clause::Learnt>(arg);
                auto &learnt_literals = learnt_clauses[clause_variant.learnt_clause_id];
                return std::array<Literal, 2>{learnt_literals[0], learnt_literals[1]};
            } else if constexpr (std::is_same_v<T, Clause::Requires>) {
                auto clause_variant = std::any_cast<Clause::Requires>(arg);
                if (watched_literals_[0] == clause_variant.parent) {
                    return literals(false, true);
                } else if (watched_literals_[1] == clause_variant.parent) {
                    return literals(true, false);
                } else {
                    return literals(true, true);
                }
            } else if constexpr (std::is_same_v<T, Clause::Constrains>) {
                return literals(false, false);
            } else if constexpr (std::is_same_v<T, Clause::ForbidMultipleInstances>) {
                return literals(false, false);
            } else if constexpr (std::is_same_v<T, Clause::Lock>) {
                return literals(false, false);
            }
        }, kind_);
    }

    std::optional<SolvableId> next_unwatched_variable(
            const Arena<LearntClauseId, std::vector<Literal>> &learnt_clauses,
            const FrozenMap<VersionSetId, std::vector<SolvableId>> &version_set_to_sorted_candidates,
            const DecisionMap &decision_map
    ) const {
        auto can_watch = [&](const Literal& solvable_lit) {
            return std::find(watched_literals_.begin(), watched_literals_.end(), solvable_lit.solvable_id) ==
                   watched_literals_.end()
                   && solvable_lit.eval(decision_map).value_or(true);
        };

         return std::visit([&](const auto &arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Clause::InstallRoot>) {
                assert(false);
                return std::optional<SolvableId>(std::nullopt);
            } else if constexpr (std::is_same_v<T, Clause::Excluded>) {
                assert(false);
                return std::optional<SolvableId>(std::nullopt);
            } else if constexpr (std::is_same_v<T, Clause::Learnt>) {
                auto clause_variant = std::any_cast<Clause::Learnt>(arg);
                auto &literals = learnt_clauses[clause_variant.learnt_clause_id];
                auto it = std::find_if(literals.begin(), literals.end(), can_watch);
                if (it != literals.end()) {
                    return std::optional<SolvableId>(it->solvable_id);
                }
                return std::optional<SolvableId>(std::nullopt);
            } else if constexpr (std::is_same_v<T, Clause::Requires>) {
                auto clause_variant = std::any_cast<Clause::Requires>(arg);
                auto parent = clause_variant.parent;
                auto version_set_id = clause_variant.requirement;

                Literal parent_literal(parent, true);
                if (can_watch(parent_literal)) {
                    return std::optional<SolvableId>(parent);
                }

                auto optional_candidates = version_set_to_sorted_candidates.get(version_set_id);
                if (!optional_candidates.has_value()) {
                    return std::optional<SolvableId>(std::nullopt);
                }

                auto &candidates = optional_candidates.value();
                auto it = std::find_if(candidates.cbegin(), candidates.cend(), [&](const SolvableId &candidate) {
                    return can_watch(Literal(candidate, false));
                });

                if (it != candidates.end()) {
                    return std::optional<SolvableId>(*it);
                }

                return std::optional<SolvableId>(std::nullopt);
            } else if constexpr (std::is_same_v<T, Clause::Constrains>) {
                return std::optional<SolvableId>(std::nullopt);
            } else if constexpr (std::is_same_v<T, Clause::ForbidMultipleInstances>) {
                return std::optional<SolvableId>(std::nullopt);
            } else if constexpr (std::is_same_v<T, Clause::Lock>) {
                return std::optional<SolvableId>(std::nullopt);
            }
        }, kind_);
    }

    ClauseState(const ClauseState &) = default;

    ClauseState &operator=(const ClauseState &) = default;

    bool has_watches() const {
        return watched_literals_[0] != 0;
    }

    const ClauseVariant &get_kind() const {
        return kind_;
    }



    // Visit literals in the clause
    void visit_literals(
            const Arena<LearntClauseId, std::vector<Literal>> &learnt_clauses,
            const FrozenMap<VersionSetId, std::vector<SolvableId>> &version_set_to_sorted_candidates,
            const VisitFunction &visit
    ) const {
        std::visit([&visit, &learnt_clauses, &version_set_to_sorted_candidates](const auto &arg) {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, Clause::InstallRoot>) {
                // do nothing
            } else if constexpr (std::is_same_v<T, Clause::Excluded>) {
                auto clause_variant = std::any_cast<Clause::Excluded>(arg);
                visit(Literal(clause_variant.candidate, true));
            } else if constexpr (std::is_same_v<T, Clause::Learnt>) {
                auto clause_variant = std::any_cast<Clause::Learnt>(arg);
                for (const Literal& literal: learnt_clauses[clause_variant.learnt_clause_id]) {
                    visit(literal);
                }
            } else if constexpr (std::is_same_v<T, Clause::Requires>) {
                auto clause_variant = std::any_cast<Clause::Requires>(arg);
                visit(Literal(clause_variant.parent, true));
                auto optional_sorted_candidates = version_set_to_sorted_candidates.get(clause_variant.requirement);
                if (optional_sorted_candidates.has_value()) {
                    for (const SolvableId &id: optional_sorted_candidates.value()) {
                        visit(Literal(id, false));
                    }
                }
            } else if constexpr (std::is_same_v<T, Clause::Constrains>) {
                auto clause_variant = std::any_cast<Clause::Constrains>(arg);
                visit(Literal(clause_variant.parent, true));
                visit(Literal(clause_variant.forbidden_solvable, true));
            } else if constexpr (std::is_same_v<T, Clause::ForbidMultipleInstances>) {
                auto clause_variant = std::any_cast<Clause::ForbidMultipleInstances>(arg);
                visit(Literal(clause_variant.candidate, true));
                visit(Literal(clause_variant.constrained_candidate, true));
            } else if constexpr (std::is_same_v<T, Clause::Lock>) {
                auto clause_variant = std::any_cast<Clause::Lock>(arg);
                visit(Literal(SolvableId::root(), true));
                visit(Literal(clause_variant.other_candidate, true));
            }

        }, kind_);
    }


    std::array<SolvableId, 2> watched_literals_;
private:
    static ClauseState from_kind_and_initial_watches(
            const ClauseVariant &kind,
            const std::optional<std::array<SolvableId, 2>> &optional_watched_literals
    ) {
        auto watched_literals = optional_watched_literals.has_value() ? optional_watched_literals.value()
                                                                      : std::array<SolvableId, 2>{SolvableId::null(),
                                                                                                  SolvableId::null()};
        ClauseState clause(
                watched_literals,
                std::array<ClauseId, 2>{ClauseId::null(), ClauseId::null()},
                kind
        );

        DBG(assert(!clause.has_watches() || watched_literals[0] != watched_literals[1]));
        return clause;
    }

    std::array<ClauseId, 2> next_watches_;
    ClauseVariant kind_;
};


#endif // CLAUSE_H