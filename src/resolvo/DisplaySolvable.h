#ifndef DISPLAY_SOLVABLE_H
#define DISPLAY_SOLVABLE_H

#include "Solvable.h"
#include "Pool.h"

// The DisplaySolvable class template is used to visualize a solvable
template<typename VS, typename N>
class DisplaySolvable {
private:
    const Pool<VS, N> &pool;
    const InternalSolvable<typename VS::V> &solvable;

public:
// Constructor
    DisplaySolvable(const Pool<VS, N> &poolRef, const InternalSolvable<typename VS::V> &solvableRef)
            : pool(poolRef), solvable(solvableRef) {}

// Display function
    void display() const {
        if (solvable.is_root()) {
            std::cout << "<root>";
        } else {
            const auto *solv = solvable.get_solvable();
// Assuming PackageName has an operator<< for display
            std::cout << pool.resolve_package_name(solv->get_name_id()) << "=" << solv->get_inner();
        }
    }
};

#endif // DISPLAY_SOLVABLE_H