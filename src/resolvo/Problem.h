#ifndef PROBLEM_H
#define PROBLEM_H

#include <iostream>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <variant>
#include <memory>
#include <algorithm>
#include "internal/ClauseId.h"
#include "internal/SolvableId.h"

class Problem {
private:
    std::vector<ClauseId> clauses;
public:
    Problem() = default;

    void add_clause(const ClauseId& clauseId) {
        if (std::find(clauses.cbegin(), clauses.cend(), clauseId) == clauses.cend()) {
            clauses.push_back(clauseId);
        }
    }
};


#endif // PROBLEM_H