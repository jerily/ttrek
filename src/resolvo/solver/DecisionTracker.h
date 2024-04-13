#include <vector>
#include <optional>
#include <cstdint> // For uint32_t
#include "../internal/SolvableId.h"
#include "../internal/ClauseId.h"
#include "Decision.h"
#include "DecisionMap.h"

class DecisionTracker {
private:
    DecisionMap map;
    std::vector<Decision> stack;
    size_t propagate_index;

public:
    DecisionTracker() : map(), stack(), propagate_index(0) {}

    void clear() {
        map = DecisionMap();
        stack.clear();
        propagate_index = 0;
    }

    bool is_empty() const {
        return stack.empty();
    }

    std::optional<bool> assigned_value(const SolvableId& solvable_id) const {
        return map.value(solvable_id);
    }

    const DecisionMap& get_map() const {
        return map;
    }

    auto get_stack() const {
        return stack;
    }

    uint32_t get_level(const SolvableId& solvable_id) const {
        return map.level(solvable_id);
    }

    std::optional<ClauseId> find_clause_for_assignment(const SolvableId& solvable_id) const {
        auto it = std::find_if(stack.rbegin(), stack.rend(), [solvable_id](const Decision& d) {
            return d.solvable_id == solvable_id;
        });
        if (it != stack.rend()) {
            return it->derived_from;
        }
        return std::nullopt;
    }

    // Attempts to add a decision
    //
    // Returns true if the solvable was undecided, false if it was already decided to the same value
    //
    // Returns an error if the solvable was decided to a different value (which means there is a conflict)
    std::optional<bool> try_add_decision(const Decision& decision, uint32_t level) {
        fprintf(stderr, "try_add_decision: solvable_id=%zd, value=%b, level=%d\n", decision.solvable_id.to_usize(), decision.value, level);
        auto optional_assigned_value = assigned_value(decision.solvable_id);
        if (!optional_assigned_value.has_value()) {
            map.set(decision.solvable_id, decision.value, level);
            stack.push_back(decision);
            return {true};
        } else if (optional_assigned_value.value() == decision.value) {
            return {false};
        } else {
            fprintf(stderr, "conflict HERE: assigned=%b vs decision=%b\n", optional_assigned_value.value(), decision.value);
            return std::nullopt;
        }
    }

    void undo_until(uint32_t level) {
        while (!stack.empty() && get_level(stack.back().solvable_id) > level) {
            undo_last();
        }
    }

    std::pair<Decision, uint32_t> undo_last() {
        if (stack.empty()) {
            throw std::logic_error("Undo stack is empty");
        }
        Decision decision = stack.back();
        stack.pop_back();
        map.reset(decision.solvable_id);

        propagate_index = stack.size();

        if (!stack.empty()) {
            const Decision& top_decision = stack.back();
            return std::make_pair(decision, map.level(top_decision.solvable_id));
        }
        return std::make_pair(decision, 0);
    }

    std::optional<Decision> next_unpropagated() {
        if (propagate_index < stack.size()) {
            return stack[propagate_index++];
        }
        return std::nullopt;
    }
};
