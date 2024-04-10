#ifndef PROPAGATIONERROR_H
#define PROPAGATIONERROR_H

#include "../internal/SolvableId.h"
#include "../internal/ClauseId.h"
#include <any>

enum class PropagationErrorType { Conflict, Cancelled };

class PropagationError {
public:
    explicit PropagationError(PropagationErrorType type)
            : type_(type) {}

protected:
    PropagationErrorType type_;
};

class ConflictPropagationError : public PropagationError {
public:
    explicit ConflictPropagationError(SolvableId solvable_id, bool is_positive, ClauseId clause_id)
            : PropagationError(PropagationErrorType::Conflict), solvable_id_(std::move(solvable_id)),
              is_positive_(is_positive), clause_id_(std::move(clause_id)) {}
private:
    SolvableId solvable_id_; // Only valid if type_ == Type::Conflict
    bool is_positive_;       // Only valid if type_ == Type::Conflict
    ClauseId clause_id_;     // Only valid if type_ == Type::Conflict
};

class CancelledPropagationError : public PropagationError {
public:
    explicit CancelledPropagationError(std::any data)
            : PropagationError(PropagationErrorType::Cancelled), data_(std::move(data)) {}
private:
    std::any data_; // Only valid if type_ == Type::Cancelled
};

#endif // PROPAGATIONERROR_H