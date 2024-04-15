#ifndef WATCHMAP_H
#define WATCHMAP_H

#include <iostream>
#include <unordered_map>
#include <vector>
#include "../internal/SolvableId.h"
#include "../internal/ClauseId.h"
#include "Clause.h"

class WatchMap {
private:
    std::unordered_map<SolvableId, ClauseId> map;

public:
    WatchMap() = default;

    void start_watching(ClauseState &clause, const ClauseId &clauseId) {
        for (size_t watch_index = 0; watch_index < clause.watched_literals_.size(); ++watch_index) {
            SolvableId watched_solvable = clause.watched_literals_[watch_index];
            ClauseId already_watching = first_clause_watching_solvable(watched_solvable);
            fprintf(stderr, "already_watching for watch_index=%zd is_null: %d\n", watch_index, already_watching.is_null());
            clause.link_to_clause(watch_index, already_watching);
            watch_solvable(watched_solvable, clauseId);
        }
    }

    void update_watched(std::optional<ClauseState> &predecessorClause, ClauseState &clause, const ClauseId& clause_id, size_t watch_index,
                        const SolvableId &previous_watch, const SolvableId &new_watch) {
        fprintf(stderr, "................................................................ update_watched\n");
        // Remove this clause from its current place in the linked list, because we
        // are no longer watching what brought us here
        if (predecessorClause.has_value()) {
            fprintf(stderr, "unlink the clause\n");
            // Unlink the clause
            predecessorClause.value().unlink_clause(clause, previous_watch, watch_index);
        } else {
            fprintf(stderr, "first clause in the chain\n");
            // This was the first clause in the chain
            map.insert({previous_watch, clause.get_linked_clause(watch_index)});
        }

        // Set the new watch
        fprintf(stderr, "set the new watch: %zd previous_watch: %zd\n", new_watch.to_usize(), previous_watch.to_usize());
        clause.watched_literals_[watch_index] = new_watch;
        clause.link_to_clause(watch_index, map.at(new_watch));
        map.insert({new_watch, clause_id});
    }

    ClauseId first_clause_watching_solvable(const SolvableId& watched_solvable) {
        return map.find(watched_solvable) != map.end() ? map.at(watched_solvable) : ClauseId::null();
    }

    void watch_solvable(const SolvableId& watched_solvable, const ClauseId& id) {
        map.insert({watched_solvable, id});
    }
};

#endif // WATCHMAP_H