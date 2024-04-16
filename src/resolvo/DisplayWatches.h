#ifndef DISPLAY_WATCHES_H
#define DISPLAY_WATCHES_H

#include <sstream>
#include "Solvable.h"
#include "Pool.h"
#include "solver/Clause.h"
#include "solver/WatchMap.h"
#include "DisplaySolvable.h"
#include "DisplayClause.h"

// The DisplaySolvable class template is used to visualize a solvable
template<typename VS, typename N>
class DisplayWatches {
private:
    std::shared_ptr<Pool<VS, N>> pool;
    WatchMap watches;
    Arena<ClauseId, ClauseState> clauses_;

public:
// Constructor
    explicit DisplayWatches(std::shared_ptr<Pool<VS, N>> poolRef, const WatchMap &watchesRef, const Arena<ClauseId, ClauseState> &clauses)
            : pool(poolRef), watches(watchesRef), clauses_(clauses) {}

    friend std::ostream &operator<<(std::ostream &os, const DisplayWatches &display_watches) {
        os << display_watches.to_string();
        return os;
    }

    std::string to_string() {
        std::ostringstream oss;
        auto map = watches.get_map();
        std::cout << "&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&" << std::endl;
        auto it = map.iter();
        while (it.has_next()) {
            auto const& [solvable_id, clause_id] = it.next().value();
            if (clause_id.is_null()) {
                continue;
            }
            auto display_solvable = DisplaySolvable<VS, N>(pool, pool->resolve_internal_solvable(solvable_id));
            auto clause = clauses_[clause_id];
            auto display_clause = DisplayClause<VS, N>(pool, clause);
            oss << display_solvable.to_string() << " is watched by clause " << display_clause.to_string() << std::endl;
            for (size_t watch = 0; watch < clause.next_watches_.size(); ++watch) {
                auto watch_clause_id = clause.next_watches_[watch];
                if (watch_clause_id.is_null()) {
                    continue;
                }
                auto watch_clause = clauses_[watch_clause_id];
                auto display_clause = DisplayClause<VS, N>(pool, watch_clause);
                oss << "  " << display_clause.to_string() << std::endl;
            };
        }
        return oss.str();
    }

};

#endif // DISPLAY_WATCHES_H