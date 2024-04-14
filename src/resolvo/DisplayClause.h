#ifndef DISPLAY_CLAUSE_H
#define DISPLAY_CLAUSE_H

#include <sstream>
#include "Solvable.h"
#include "Pool.h"
#include "solver/Clause.h"
#include "DisplaySolvable.h"

// The DisplaySolvable class template is used to visualize a solvable
template<typename VS, typename N>
class DisplayClause {
private:
    std::shared_ptr<Pool<VS, N>> pool;
    ClauseState clause;

public:
// Constructor
    explicit DisplayClause(std::shared_ptr<Pool<VS, N>> poolRef, const ClauseState &clauseRef)
            : pool(poolRef), clause(clauseRef) {}

    friend std::ostream &operator<<(std::ostream &os, const DisplayClause &display_clause) {
        os << display_clause.to_string();
        return os;
    }

    std::string to_string() const {
        std::ostringstream oss;
        std::visit([this, &oss](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Clause::InstallRoot>) {
                oss << "install root";
            } else if constexpr (std::is_same_v<T, Clause::Excluded>) {
                auto excluded = std::any_cast<Clause::Excluded>(arg);
                auto solvable_id = excluded.candidate;
                auto reason = excluded.reason;
                auto display_solvable = DisplaySolvable<VS, N>(pool, pool->resolve_internal_solvable(solvable_id));
                oss << display_solvable.to_string() << " excluded because " << pool->resolve_string(reason);
            }
        }, clause.get_kind());
        return oss.str();
    }

};

#endif // DISPLAY_CLAUSE_H