#ifndef UNSOLVABLEORCANCELLED_H
#define UNSOLVABLEORCANCELLED_H

#include <any>
#include "../Problem.h"

enum class UnsolvableOrCancelledType {
    Unsolvable, Cancelled
};

class UnsolvableOrCancelled {
public:

    explicit UnsolvableOrCancelled(UnsolvableOrCancelledType type) : type_(type) {}

private:
    UnsolvableOrCancelledType type_;
};

class Unsolvable : public UnsolvableOrCancelled {
public:
    // For Unsolvable case
    explicit Unsolvable(Problem problem)
            : UnsolvableOrCancelled(UnsolvableOrCancelledType::Unsolvable), problem_(std::move(problem)) {}

private:
    Problem problem_; // Only valid if type_ == Type::Unsolvable
};

class Cancelled : public UnsolvableOrCancelled {
public:
    // For Cancelled case
    explicit Cancelled(std::any data)
            : UnsolvableOrCancelled(UnsolvableOrCancelledType::Cancelled), data_(std::move(data)) {}

private:
    std::any data_; // Only valid if type_ == Type::Cancelled
};

#endif // UNSOLVABLEORCANCELLED_H