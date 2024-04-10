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

    void start_watching(const ClauseState &clause, const ClauseId &clauseId) {
        for (size_t watch_index = 0; watch_index < clause.watched_literals.size(); ++watch_index) {
            SolvableId watchedSolvable = clause.watched_literals[watch_index];
            ClauseId alreadyWatching = first_clause_watching_solvable(watchedSolvable);
            clause.link_to_clause(watch_index, alreadyWatching);
            watch_solvable(watchedSolvable, clauseId);
        }
    }

    void update_watched(ClauseState *predecessorClause, ClauseState &clause, ClauseId clauseId, size_t watchIndex,
                        const SolvableId &previousWatch, const SolvableId &newWatch) {
        // Remove this clause from its current place in the linked list
        if (predecessorClause != nullptr) {
            // Unlink the clause
            predecessorClause->unlink_clause(&clause, previousWatch, watchIndex);
        } else {
            // This was the first clause in the chain
            map[previousWatch] = clause.get_linked_clause(watchIndex);
        }

        // Set the new watch
        clause.watched_literals[watchIndex] = newWatch;
        clause.link_to_clause(watchIndex, map[newWatch]);
        map[newWatch] = clauseId;
    }

    ClauseId first_clause_watching_solvable(SolvableId watchedSolvable) {
        return map.count(watchedSolvable) ? map[watchedSolvable] : ClauseId::null();
    }

    void watch_solvable(SolvableId watchedSolvable, ClauseId id) {
        map[watchedSolvable] = id;
    }
};

#endif // WATCHMAP_H