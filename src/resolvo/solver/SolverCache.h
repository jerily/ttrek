#ifndef SOLVER_CACHE_H
#define SOLVER_CACHE_H

#include <memory>
#include <any>
#include "../internal/CandidatesId.h"
#include "../internal/DependenciesId.h"
#include "../internal/FrozenCopyMap.h"
#include "../internal/Arena.h"
#include "../internal/FrozenMap.h"
#include "../Pool.h"
#include "../Problem.h"
#include "Clause.h"
#include "WatchMap.h"

class Dependencies;
class Candidates;

// Keeps a cache of previously computed and/or requested information about solvables and version
// sets.
template<typename VS, typename N, typename D>
class SolverCache {
public:
    D provider;

    // A mapping of `VersionSetId` to a sorted list of candidates that match that set.
    FrozenMap<VersionSetId, std::vector<SolvableId>> version_set_to_sorted_candidates;

    explicit SolverCache(D provider) : provider(provider) {}

    // Returns a reference to the pool used by the solver
    const Pool<VS, N>& get_pool() {
        return provider.pool();
    }

    // Returns the candidates for the package with the given name. This will either ask the
    // [`DependencyProvider`] for the entries or a cached value.
    // If the provider has requested the solving process to be cancelled, the cancellation value
    // will be returned as an `Err(...)`.
    const Candidates& get_or_cache_candidates(NameId package_name) {
        // If we already have the candidates for this package cached we can simply return
        auto candidates_id = package_name_to_candidates.get_copy(package_name);
        if (candidates_id.has_value()) {
            return candidates[candidates_id.value()];
        } else {
            // Since getting the candidates from the provider is a potentially blocking
            // operation, we want to check beforehand whether we should cancel the solving
            // process
            if (provider.should_cancel_with_value().has_value()) {
               throw std::runtime_error("Solver cancelled");
            }

            // Check if there is an in-flight request
            auto in_flight_request = package_name_to_candidates_in_flight.find(package_name);
            if (in_flight_request != package_name_to_candidates_in_flight.end()) {
                // Found an in-flight request, wait for that request to finish and return the computed result.
                in_flight_request->second->listen();
                return candidates[package_name_to_candidates.get_copy(package_name).value()];
            } else {
                // Prepare an in-flight notifier for other requests coming in.
                package_name_to_candidates_in_flight[package_name] = std::make_shared<Event>();

                // Otherwise we have to get them from the DependencyProvider
                auto candidates = provider.get_candidates(package_name).value_or(Candidates());
                // Store information about which solvables dependency information is easy to
                // retrieve.
                for (auto hint_candidate : candidates.hint_dependencies_available) {
                    auto idx = hint_candidate.to_usize();
                    if (hint_dependencies_available.size() <= idx) {
                        hint_dependencies_available.resize(idx + 1, false);
                    }
                    hint_dependencies_available[idx] = true;
                }

                // Allocate an ID so we can refer to the candidates from everywhere
                auto candidates_id = this->candidates.alloc(candidates);
                package_name_to_candidates.insert_copy(package_name, candidates_id);

                // Remove the in-flight request now that we inserted the result and notify any waiters
                auto notifier = package_name_to_candidates_in_flight[package_name];
                package_name_to_candidates_in_flight.erase(package_name);
                notifier->notify(std::numeric_limits<std::size_t>::max());

                return candidates[candidates_id];
            }
        }
    }

    // Returns the candidates of a package that match the specified version set.
    // If the provider has requested the solving process to be cancelled, the cancellation value
    // will be returned as an `Err(...)`.
    std::vector<SolvableId> get_or_cache_matching_candidates(VersionSetId version_set_id) {
        auto candidates = version_set_candidates.get(version_set_id);
        if (candidates.has_value()) {
            return candidates.value();
        } else {
            auto package_name = provider.pool().resolve_version_set_package_name(version_set_id);
            auto version_set = provider.pool().resolve_version_set(version_set_id);
            auto candidates = get_or_cache_candidates(package_name);
            std::vector<SolvableId> matching_candidates;
            for (auto p : candidates->candidates) {
                auto version = provider.pool().resolve_internal_solvable(p).solvable().inner();
                if (version_set.contains(version)) {
                    matching_candidates.push_back(p);
                }
            }
            version_set_candidates.insert(version_set_id, matching_candidates);
            return matching_candidates;
        }
    }

    // Returns the candidates that do *not* match the specified requirement.
    // If the provider has requested the solving process to be cancelled, the cancellation value
    // will be returned as an `Err(...)`.
    std::vector<SolvableId> get_or_cache_non_matching_candidates(VersionSetId version_set_id) {
        auto candidates = version_set_inverse_candidates.get(version_set_id);
        if (candidates.has_value()) {
            return candidates.value();
        } else {
            auto package_name = provider.pool().resolve_version_set_package_name(version_set_id);
            auto version_set = provider.pool().resolve_version_set(version_set_id);
            auto candidates = get_or_cache_candidates(package_name);
            std::vector<SolvableId> non_matching_candidates;
            for (auto p : candidates->candidates) {
                auto version = provider.pool().resolve_internal_solvable(p).solvable().inner();
                if (!version_set.contains(version)) {
                    non_matching_candidates.push_back(p);
                }
            }
            version_set_inverse_candidates.insert(version_set_id, non_matching_candidates);
            return non_matching_candidates;
        }
    }

    // Returns the candidates for the package with the given name similar to
    // [`Self::get_or_cache_candidates`] sorted from highest to lowest.
    //
    // If the provider has requested the solving process to be cancelled, the cancellation value
    // will be returned as an `Err(...)`.
    std::vector<SolvableId> get_or_cache_sorted_candidates(VersionSetId version_set_id) {
        auto candidates = version_set_to_sorted_candidates.get(version_set_id);
        if (candidates.has_value()) {
            return candidates.value();
        } else {
            auto package_name = provider.pool().resolve_version_set_package_name(version_set_id);
            auto matching_candidates = get_or_cache_matching_candidates(version_set_id);
            auto candidates = get_or_cache_candidates(package_name);
            std::vector<SolvableId> sorted_candidates;
            sorted_candidates.insert(sorted_candidates.end(), matching_candidates.begin(), matching_candidates.end());
            provider.sort_candidates(this, sorted_candidates);
            auto favored_id = candidates->favored;
            if (favored_id.has_value()) {
                auto pos = std::find(sorted_candidates.begin(), sorted_candidates.end(), favored_id.value_());
                if (pos != sorted_candidates.end()) {
                    std::rotate(sorted_candidates.begin(), pos, pos + 1);
                }
            }
            version_set_to_sorted_candidates.insert(version_set_id, sorted_candidates);
            return sorted_candidates;
        }
    }

private:
    // A mapping from package name to a list of candidates.
    Arena<CandidatesId, Candidates> candidates;
    FrozenCopyMap<NameId, CandidatesId> package_name_to_candidates;
    std::map<NameId, Rc<Event>> package_name_to_candidates_in_flight;

    // A mapping of `VersionSetId` to the candidates that match that set.
    FrozenMap<VersionSetId, std::vector<SolvableId>> version_set_candidates;

    // A mapping of `VersionSetId` to the candidates that do not match that set (only candidates
    // of the package indicated by the version set are included).
    FrozenMap<VersionSetId, std::vector<SolvableId>> version_set_inverse_candidates;

    // A mapping from a solvable to a list of dependencies
    Arena<DependenciesId, Dependencies> solvable_dependencies;
    FrozenCopyMap<SolvableId, DependenciesId> solvable_to_dependencies;

    // A mapping that indicates that the dependencies for a particular solvable can cheaply be
    // retrieved from the dependency provider. This information is provided by the
    // DependencyProvider when the candidates for a package are requested.
    std::vector<bool> hint_dependencies_available;

//    _data: PhantomData<(VS, N)>,

};

#endif // SOLVER_CACHE_H