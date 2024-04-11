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

#include "../internal/SolvableId.h"
#include "../internal/VersionSetId.h"
#include "../internal/LearntClauseId.h"
#include "../internal/StringId.h"
#include "../internal/Arena.h"
#include "../internal/ClauseId.h"
#include "../internal/FrozenCopyMap.h"
#include "DecisionTracker.h"
#include "DecisionMap.h"


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

enum class ClauseType {
    InstallRoot,
    Requires,
    ForbidMultipleInstances,
    Constrains,
    Lock,
    Learnt,
    Excluded
};

class RequiresClause;

class ConstrainsClause;

class ForbidMultipleInstancesClause;

class RootClause;

class ExcludedClause;

class LockClause;

class LearntClause;

class Clause {
public:

    Clause(ClauseType type) : type_(type) {}

    ClauseType get_type() const {
        return type_;
    }

// Factory methods for creating different kinds of clauses

    static std::tuple<RequiresClause, std::optional<std::array<SolvableId, 2>>, bool> requires(
            const SolvableId &parent,
            VersionSetId requirement,
            const std::vector<SolvableId> &candidates,
            const DecisionTracker &decision_tracker
    );

    static std::tuple<ConstrainsClause, std::optional<std::array<SolvableId, 2>>, bool> constrains(
            const SolvableId &parent,
            const SolvableId &forbidden_solvable,
            const VersionSetId &via,
            const DecisionTracker &decision_tracker
    );

    static std::pair<ForbidMultipleInstancesClause, std::optional<std::array<SolvableId, 2>>> forbid_multiple(
            const SolvableId &candidate,
            const SolvableId &constrained_candidate
    );

    static std::pair<RootClause, std::optional<std::array<SolvableId, 2>>> root();

    static std::pair<ExcludedClause, std::optional<std::array<SolvableId, 2>>> exclude(
            const SolvableId &candidate,
            const StringId &reason
    );

    static std::pair<LockClause, std::optional<std::array<SolvableId, 2>>> lock(
            const SolvableId &locked_candidate,
            const SolvableId &other_candidate
    );

    static std::pair<LearntClause, std::optional<std::array<SolvableId, 2>>> learnt(
            const LearntClauseId &learnt_clause_id,
            const std::vector<Literal> &literals
    );

    void visit_literals(const Arena<LearntClauseId, std::vector<Literal>> &learnt_clauses,
                        const std::map<VersionSetId, std::vector<SolvableId>> &version_set_to_sorted_candidates,
                        const VisitFunction &visit) const;

private:
    ClauseType type_;
};

class RootClause : public Clause {
public:
    explicit RootClause() : Clause(ClauseType::InstallRoot) {}
};

class RequiresClause : public Clause {
    friend class Clause;

    friend class ClauseState;

public:
    RequiresClause(SolvableId parent, VersionSetId requirement)
            : Clause(ClauseType::Requires), parent_(std::move(parent)), requirement_(std::move(requirement)) {}

protected:
    SolvableId parent_;
    VersionSetId requirement_;
};

class ForbidMultipleInstancesClause : public Clause {
    friend class Clause;

public:
    ForbidMultipleInstancesClause(SolvableId candidate, SolvableId constrained_candidate)
            : Clause(ClauseType::ForbidMultipleInstances), candidate_(std::move(candidate)),
              constrained_candidate_(std::move(constrained_candidate)) {}

protected:
    SolvableId candidate_;
    SolvableId constrained_candidate_;
};

class ConstrainsClause : public Clause {
    friend class Clause;

public:
    ConstrainsClause(SolvableId parent, SolvableId forbidden_solvable, VersionSetId via)
            : Clause(ClauseType::Constrains), parent_(std::move(parent)),
              forbidden_solvable_(std::move(forbidden_solvable)), via_(std::move(via)) {}

protected:
    SolvableId parent_;
    SolvableId forbidden_solvable_;
    VersionSetId via_;
};

class LockClause : public Clause {
    friend class Clause;

public:
    LockClause(SolvableId locked_candidate, SolvableId other_candidate)
            : Clause(ClauseType::Lock), locked_candidate_(std::move(locked_candidate)),
              other_candidate_(std::move(other_candidate)) {}

protected:
    SolvableId locked_candidate_;
    SolvableId other_candidate_;
};

class LearntClause : public Clause {
    friend class Clause;

    friend class ClauseState;

public:
    LearntClause(LearntClauseId learnt_clause_id, const std::vector<Literal> &literals)
            : Clause(ClauseType::Learnt), learnt_clause_id_(std::move(learnt_clause_id)), literals_(literals) {}

    LearntClauseId learnt_clause_id_;
protected:
    std::vector<Literal> literals_;
};

class ExcludedClause : public Clause {
    friend class Clause;

public:
    ExcludedClause(SolvableId candidate, StringId reason)
            : Clause(ClauseType::Excluded), candidate_(std::move(candidate)), reason_(std::move(reason)) {}

protected:
    SolvableId candidate_;
    StringId reason_;
};

std::tuple<RequiresClause, std::optional<std::array<SolvableId, 2>>, bool> Clause::requires(
        const SolvableId &parent,
        VersionSetId requirement,
        const std::vector<SolvableId> &candidates,
        const DecisionTracker &decision_tracker
) {
    // It only makes sense to introduce a requires clause when the parent solvable is undecided
    // or going to be installed
    assert(decision_tracker.assigned_value(parent) != false);

    auto kind = RequiresClause(parent, std::move(requirement));
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


std::tuple<ConstrainsClause, std::optional<std::array<SolvableId, 2>>, bool> Clause::constrains(
        const SolvableId &parent,
        const SolvableId &forbidden_solvable,
        const VersionSetId &via,
        const DecisionTracker &decision_tracker
) {
    assert(decision_tracker.assigned_value(parent) != false);

    bool conflict = decision_tracker.assigned_value(forbidden_solvable) == true;
    auto kind = ConstrainsClause(parent, forbidden_solvable, via);

    // return clause, (parent, forbidden_solvable), conflict
    return std::make_tuple(kind, std::array<SolvableId, 2>{parent, forbidden_solvable}, conflict);
}


std::pair<ForbidMultipleInstancesClause, std::optional<std::array<SolvableId, 2>>> Clause::forbid_multiple(
        const SolvableId &candidate,
        const SolvableId &constrained_candidate
) {
    auto kind = ForbidMultipleInstancesClause(candidate, constrained_candidate);
    return std::make_pair(kind, std::array<SolvableId, 2>{candidate, constrained_candidate});
}

std::pair<RootClause, std::optional<std::array<SolvableId, 2>>> Clause::root() {
    auto kind = RootClause();
    return std::make_pair(kind, std::nullopt);
}


std::pair<ExcludedClause, std::optional<std::array<SolvableId, 2>>> Clause::exclude(
        const SolvableId &candidate,
        const StringId &reason
) {
    auto kind = ExcludedClause(candidate, reason);
    return std::make_pair(kind, std::nullopt);
}

std::pair<LockClause, std::optional<std::array<SolvableId, 2>>> Clause::lock(
        const SolvableId &locked_candidate,
        const SolvableId &other_candidate
) {
    auto kind = LockClause(locked_candidate, other_candidate);
    return std::make_pair(kind, std::array<SolvableId, 2>{SolvableId::root(), other_candidate});
}

std::pair<LearntClause, std::optional<std::array<SolvableId, 2>>> Clause::learnt(
        const LearntClauseId &learnt_clause_id,
        const std::vector<Literal> &literals
) {
    assert(!literals.empty());

    auto kind = LearntClause(learnt_clause_id, literals);

    std::optional<std::array<SolvableId, 2>> watches;

    if (literals.size() == 1) {
        return std::make_pair(kind, std::nullopt);
    } else {
        watches = {literals.front().solvable_id, literals.back().solvable_id};
    }

    return std::make_pair(kind, watches);
}


// Visit literals in the clause
void Clause::visit_literals(
        const Arena<LearntClauseId, std::vector<Literal>> &learnt_clauses,
        const std::map<VersionSetId, std::vector<SolvableId>> &version_set_to_sorted_candidates,
        const VisitFunction &visit
) const {
    switch (type_) {
        case ClauseType::InstallRoot:
            // Do nothing for InstallRoot
            break;
        case ClauseType::Excluded:
            visit(Literal(static_cast<const ExcludedClause *>(this)->candidate_, true));
            break;
        case ClauseType::Learnt:
            for (Literal literal: learnt_clauses[static_cast<const LearntClause *>(this)->learnt_clause_id_]) {
                visit(literal);
            }
            break;
        case ClauseType::Requires:
            visit(Literal(static_cast<const RequiresClause *>(this)->parent_, true));
            for (const SolvableId &id: version_set_to_sorted_candidates.at(
                    static_cast<const RequiresClause *>(this)->requirement_)) {
                visit(Literal(id, false));
            }
            break;
        case ClauseType::Constrains:
            visit(Literal(static_cast<const ConstrainsClause *>(this)->parent_, true));
            visit(Literal(static_cast<const ConstrainsClause *>(this)->forbidden_solvable_, true));
            break;
        case ClauseType::ForbidMultipleInstances:
            visit(Literal(static_cast<const ForbidMultipleInstancesClause *>(this)->candidate_, true));
            visit(Literal(static_cast<const ForbidMultipleInstancesClause *>(this)->constrained_candidate_, true));
            break;
        case ClauseType::Lock:
            visit(Literal(SolvableId::root(), true));
            visit(Literal(static_cast<const LockClause *>(this)->other_candidate_, true));
            break;
    }
}

class ClauseState {
public:
    ClauseState(
            const std::array<SolvableId, 2> &watched_literals,
            const std::array<ClauseId, 2> &next_watches,
            Clause kind
    ) : watched_literals_(watched_literals), next_watches_(next_watches), kind_(kind) {}

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

        switch (kind_.get_type()) {
            case ClauseType::InstallRoot:
                assert(false);
            case ClauseType::Excluded:
                assert(false);
            case ClauseType::Learnt: {
                auto &w1 = learnt_clauses[static_cast<const LearntClause *>(&kind_)->learnt_clause_id_][0];
                auto &w2 = learnt_clauses[static_cast<const LearntClause *>(&kind_)->learnt_clause_id_][1];
                return {w1, w2};
            }
            case ClauseType::Constrains:
            case ClauseType::ForbidMultipleInstances:
            case ClauseType::Lock:
                return literals(false, false);
            case ClauseType::Requires: {
                if (watched_literals_[0] == watched_literals_[0]) {
                    return literals(false, true);
                } else if (watched_literals_[1] == watched_literals_[1]) {
                    return literals(true, false);
                } else {
                    return literals(true, true);
                }
            }
        }
    }

    std::optional<SolvableId> next_unwatched_variable(
            const Arena<LearntClauseId, std::vector<Literal>> &learnt_clauses,
            const std::map<VersionSetId, std::vector<SolvableId>> &version_set_to_sorted_candidates,
            const DecisionMap &decision_map
    ) const {
        auto can_watch = [&](Literal solvable_lit) {
            return std::find(watched_literals_.begin(), watched_literals_.end(), solvable_lit.solvable_id) ==
                   watched_literals_.end()
                   && solvable_lit.eval(decision_map).value_or(true);
        };

        switch (kind_.get_type()) {
            case ClauseType::InstallRoot:
                assert(false);
            case ClauseType::Excluded:
                assert(false);
            case ClauseType::Learnt: {
                auto &literals = learnt_clauses[static_cast<const LearntClause *>(&kind_)->learnt_clause_id_];
                auto it = std::find_if(literals.begin(), literals.end(), can_watch);
                if (it != literals.end()) {
                    return it->solvable_id;
                }
                return std::nullopt;
            }
            case ClauseType::Constrains:
            case ClauseType::ForbidMultipleInstances:
            case ClauseType::Lock:
                return std::nullopt;
            case ClauseType::Requires: {
                auto parent = static_cast<const RequiresClause *>(&kind_)->parent_;
                auto version_set_id = static_cast<const RequiresClause *>(&kind_)->requirement_;

                Literal parent_literal(parent, true);
                if (can_watch(parent_literal)) {
                    return parent;
                }

                auto &candidates = version_set_to_sorted_candidates.at(version_set_id);
                auto it = std::find_if(candidates.begin(), candidates.end(), [&](const SolvableId &candidate) {
                    return can_watch(Literal(candidate, false));
                });

                if (it != candidates.end()) {
                    return *it;
                }

                return std::nullopt;
            }
        }
    }


    ClauseState(const ClauseState &) = default;

    ClauseState &operator=(const ClauseState &) = default;

    bool has_watches() const {
        return watched_literals_[0] != 0;
    }

    const Clause& get_kind() const {
        return kind_;
    }


    std::array<SolvableId, 2> watched_literals_;
private:
    static ClauseState from_kind_and_initial_watches(
            const Clause &kind,
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

        assert(!clause.has_watches() || watched_literals[0] != watched_literals[1]);
        return clause;
    }

    std::array<ClauseId, 2> next_watches_;
    Clause kind_;
};

