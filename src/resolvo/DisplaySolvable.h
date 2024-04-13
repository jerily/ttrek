#ifndef DISPLAY_SOLVABLE_H
#define DISPLAY_SOLVABLE_H

#include "Solvable.h"
#include "Pool.h"

// The DisplaySolvable class template is used to visualize a solvable
template<typename VS, typename N>
class DisplaySolvable {
private:
    const Pool<VS, N> &pool;
    const InternalSolvable<typename VS::ValueType> &solvable;

public:
// Constructor
    explicit DisplaySolvable(const Pool<VS, N> &poolRef, const InternalSolvable<typename VS::ValueType> &solvableRef)
            : pool(poolRef), solvable(solvableRef) {}

    friend std::ostream &operator<<(std::ostream &os, const DisplaySolvable &display_solvable) {
        if (display_solvable.solvable.is_root()) {
            os << "<root>";
        } else {
            const auto &solv = display_solvable.solvable.get_solvable_unchecked();
            os << display_solvable.pool.resolve_package_name(solv.get_name_id()) << "=" << solv.get_inner();
        }
        return os;
    }

};

#endif // DISPLAY_SOLVABLE_H