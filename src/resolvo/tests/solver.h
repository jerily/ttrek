#ifndef SOLVER_H
#define SOLVER_H

#include "../internal/NameId.h"
#include "../internal/VersionSetId.h"
#include "../Common.h"
#include "../Range.h"
#include "../Pool.h"
#include "../DisplaySolvable.h"
#include "../solver/Solver.h"
#include <cstdint>
#include <unordered_set>
#include <sstream>

// Let's define our own packaging version system and dependency specification.
// This is a very simple version system, where a package is identified by a name and a version
// in which the version is just an integer. The version is a range so can be noted as 0..2
// or something of the sorts, we also support constrains which means it should not use that
// package version this is also represented with a range.
//
// You can also use just a single number for a range like `package 0` which means the range from 0..1 (excluding the end)
//
// Lets call the tuples of (Name, Version) a `Pack` and the tuples of (Name, Range<u32>) a `Spec`
//
// We also need to create a custom provider that tells us how to sort the candidates. This is unique to each
// packaging ecosystem. Let's call our ecosystem 'BundleBox' so that how we call the provider as well.

// This is `Pack` which is a unique version and name in our bespoke packaging system
struct Pack {
    uint32_t version;
    bool unknown_deps;
    bool cancel_during_get_dependencies;

    explicit Pack(uint32_t p_version, bool p_unknown_deps = false, bool p_cancel_during_get_dependencies = false)
            : version(p_version), unknown_deps(p_unknown_deps),
              cancel_during_get_dependencies(p_cancel_during_get_dependencies) {

        fprintf(stderr, "****************** cancel_during_get_dependencies: %d\n", p_cancel_during_get_dependencies);
    }

    // assignment operator
    Pack &operator=(const Pack &other) {
        version = other.version;
        unknown_deps = other.unknown_deps;
        cancel_during_get_dependencies = other.cancel_during_get_dependencies;
        return *this;
    }

    // copy constructor
    Pack(const Pack &other) {
        version = other.version;
        unknown_deps = other.unknown_deps;
        cancel_during_get_dependencies = other.cancel_during_get_dependencies;
    }

//    Pack with_unknown_deps() const {
//        return Pack(version, true, cancel_during_get_dependencies);
//    }
//
//    Pack with_cancel_during_get_dependencies() const {
//        return Pack(version, unknown_deps, true);
//    }

    Pack offset(uint32_t offset) {
        version = version + offset;
        return *this;
    }

    bool operator==(const Pack &other) const {
        return version == other.version;
    }

    bool operator<(const Pack &other) const {
        return version < other.version;
    }

    bool operator>(const Pack &other) const {
        return version > other.version;
    }

    bool operator<=(const Pack &other) const {
        return version <= other.version;
    }

    bool operator>=(const Pack &other) const {
        return version >= other.version;
    }

    friend std::ostream &operator<<(std::ostream &os, const Pack &pack) {
        os << "Pack(" << pack.version << ")";
        return os;
    }
};

namespace std {
    template<>
    struct hash<Pack> {
        std::size_t operator()(const Pack &pack) const {
            return std::hash<uint32_t>()(pack.version);
        }
    };
}

std::vector<std::string> split_string(const std::string &str, const char *split_str) {
    std::vector<std::string> split;
    size_t start = 0;
    size_t end = str.find(split_str);
    while (end != std::string::npos) {
        split.push_back(str.substr(start, end - start));
        start = end + 1;
        end = str.find(split_str, start);
    }
    split.push_back(str.substr(start, end));
    return split;
}

struct Spec {
    std::string name;
    Range<Pack> versions;

    static Spec from_str(const std::string &s) {
        auto split = split_string(s, " ");
        auto spec_name = split[0];

        auto version_range = [](const std::optional<std::string> &s) -> Range<Pack> {
            if (!s.has_value()) {
                return Range<Pack>::full();
            }
            auto split = split_string(s.value(), "..");
            auto start = Pack(std::stoi(split[0]));
            auto end = split.size() == 1 ? start.offset(1) : Pack(std::stoi(split[1]));
            return Range<Pack>::between(start, end);
        };

        // print all split
        for (const auto &s0: split) {
            std::cout << s0 << std::endl;
        }
        auto spec_versions = version_range(split.size() > 1 ? std::optional(split[1]) : std::nullopt);
        return Spec{spec_name, spec_versions};
    }
};

struct BundleBoxPackageDependencies {
    std::vector<Spec> dependencies;
    std::vector<Spec> constrains;

};

class BundleBoxProvider {
public:

    std::shared_ptr<Pool<Range<Pack>>> pool = std::make_shared<Pool<Range<Pack>>>(Pool<Range<Pack>>());
    std::unordered_map<std::string, std::unordered_map<Pack, BundleBoxPackageDependencies>> packages;
    std::unordered_map<std::string, Pack> favored;
    std::unordered_map<std::string, Pack> locked;
    std::unordered_map<std::string, std::unordered_map<Pack, std::string>> excluded;
    bool cancel_solving;
//    std::atomic<size_t> concurrent_requests;
//    std::shared_ptr<std::atomic<size_t>> concurrent_requests_max;
    bool sleep_before_return;
    std::unordered_set<NameId> requested_candidates;
    std::unordered_set<NameId> requested_dependencies;

    std::vector<VersionSetId> requirements(const std::vector<std::string> &requirements) {
        std::vector<VersionSetId> version_set_ids;
        for (const auto &dep: requirements) {
            auto spec = Spec::from_str(dep);
            auto dep_name = pool->intern_package_name(spec.name);
            version_set_ids.push_back(pool->intern_version_set(dep_name, spec.versions));
        }
        return version_set_ids;
    }

    static BundleBoxProvider
    from_packages(const std::vector<std::tuple<std::string, uint32_t, std::vector<std::string>>> &p_packages) {
        auto result = BundleBoxProvider();
        for (const auto &package: p_packages) {
            auto name = std::get<0>(package);
            auto version = std::get<1>(package);
            auto deps = std::get<2>(package);
            result.add_package(name, Pack(version), deps, {});
        }
        return result;
    }

    void set_favored(const std::string &package_name, uint32_t version) {
        favored.insert(std::make_pair(package_name, Pack(version)));
    }

    void exclude(const std::string &package_name, uint32_t version, const std::string &reason) {
        excluded.at(package_name).insert(std::make_pair(Pack(version), reason));
    }

    void set_locked(const std::string &package_name, uint32_t version) {
        locked.insert(std::make_pair(package_name, Pack(version)));
    }

    void add_package(const std::string &package_name, Pack package_version,
                     const std::vector<std::string> &package_dependencies,
                     const std::vector<std::string> &package_constrains) {
        std::vector<Spec> deps;
        for (const auto &dep: package_dependencies) {
            deps.push_back(Spec::from_str(dep));
        }

        std::vector<Spec> cons;
        for (const auto &con: package_constrains) {
            cons.push_back(Spec::from_str(con));
        }

        packages[package_name].insert(std::make_pair(package_version, BundleBoxPackageDependencies{deps, cons}));
    }

    void sort_candidates(std::vector<SolvableId> &solvables) {
        std::sort(solvables.begin(), solvables.end(), [this](const SolvableId &a, const SolvableId &b) {
            auto a_pack = pool->resolve_solvable(a).get_inner();
            auto b_pack = pool->resolve_solvable(b).get_inner();
            return b_pack.version - a_pack.version;
        });
    }

    std::optional<PackageCandidates> get_candidates(const NameId &name_id) {
        // TODO

        assert(requested_candidates.insert(name_id).second);        //            "duplicate get_candidates request"

        auto package_name = pool->resolve_package_name(name_id);
        fprintf(stderr, ">>>>>>>>>>>>>>>>>>>>> get_candidates %s\n", package_name.c_str());
        if (packages.find(package_name) == packages.end()) {
            return std::nullopt;
        }

        auto package = packages.at(package_name);
        PackageCandidates package_candidates;
        package_candidates.candidates.reserve(package.size());


        auto favored_pack =
                favored.find(package_name) != favored.cend() ? std::optional(favored.at(package_name)) : std::nullopt;
        auto locked_pack =
                locked.find(package_name) != locked.cend() ? std::optional(locked.at(package_name)) : std::nullopt;
        auto excluded_packs = excluded.find(package_name) != excluded.cend() ? std::optional(excluded.at(package_name))
                                                                             : std::nullopt;

        for (const auto &[pack, _]: package) {
            auto solvable_id = pool->intern_solvable(name_id, pack);
            fprintf(stderr, "^^^^ solvable_id: %d\n", solvable_id.to_usize());
            package_candidates.candidates.push_back(solvable_id);
            if (favored_pack.has_value() && favored_pack.value() == pack) {
                package_candidates.favored = std::optional(solvable_id);
            }
            if (locked_pack.has_value() && locked_pack.value() == pack) {
                package_candidates.locked = std::optional(solvable_id);
            }

            if (excluded_packs.has_value() && excluded_packs.value().find(pack) != excluded_packs.value().cend()) {
                package_candidates.excluded.emplace_back(solvable_id, pool->intern_string(excluded_packs.value().at(pack)));
            }
        }
        return package_candidates;
    }

    DependenciesVariant get_dependencies(const SolvableId &solvable) {
        // TODO
        fprintf(stderr, ">>>>>>>>>>>>>>>>>>>>> get_dependencies solvable_id: %lu\n", solvable.to_usize());
        auto candidate = pool->resolve_solvable(solvable);
//        fprintf(stderr, ">>>>>>>>>>>>>>>>>>>>> get_dependencies candidate: name_id=%lu\n", candidate.get_name_id().to_usize());
        auto package_name = pool->resolve_package_name(candidate.get_name_id());
        auto pack = candidate.get_inner();

        fprintf(stderr, "cancel_during_get_dependencies: version=%d %d vs %d\n", pack.version, pack.cancel_during_get_dependencies, candidate.get_inner().cancel_during_get_dependencies);

        if (pack.cancel_during_get_dependencies) {
            cancel_solving = true;
            fprintf(stderr, "##################### cancel_during_get_dependencies\n");
            return Dependencies::Unknown{pool->intern_string("cancelled")};
        }

        if (pack.unknown_deps) {
            fprintf(stderr, "##################### could not retrieve deps\n");
            return Dependencies::Unknown{pool->intern_string("could not retrieve deps")};
        }

        if (packages.find(package_name) == packages.end()) {
            return Dependencies::Known{KnownDependencies{}};
        }

        auto deps = packages.at(package_name).at(pack);
        KnownDependencies result;
        for (const auto &req: deps.dependencies) {
            auto dep_name = pool->intern_package_name(req.name);
            auto dep_spec = pool->intern_version_set(dep_name, req.versions);
            result.requirements.push_back(dep_spec);
        }

        for (const auto &req: deps.constrains) {
            auto dep_name = pool->intern_package_name(req.name);
            auto dep_spec = pool->intern_version_set(dep_name, req.versions);
            result.constrains.push_back(dep_spec);
        }

        return Dependencies::Known{result};
    }

    std::optional<std::string> should_cancel_with_value() {
        if (cancel_solving) {
            return std::optional("cancelled!");
        } else {
            return std::nullopt;
        }
    }

private:
    std::vector<Spec> dependencies;
    std::vector<Spec> constrains;
};


// Create a string from a [`Transaction`]
std::string transaction_to_string(BundleBoxProvider &provider, const std::vector<SolvableId> &solvables) {
    std::stringstream output;
    for (const auto &solvable: solvables) {
        auto display_solvable = DisplaySolvable(provider.pool,
                                                provider.pool->resolve_internal_solvable(solvable));
        output << display_solvable << "\n";
    }
    return output.str();
}

std::string solve_unsat(BundleBoxProvider &provider, const std::vector<std::string> &specs) {
    auto requirements = provider.requirements(specs);
    auto pool = provider.pool;
    auto solver = Solver<Range<Pack>, std::string, BundleBoxProvider>(provider);
    auto [steps, err] = solver.solve(requirements);
    if (err.has_value()) {
        auto problem = err.value();
// TODO:        auto graph = problem.graph(solver);
        std::stringstream output;
        output << "UNSOLVABLE:\n";
// TODO:        graph.graphviz(output, pool, true);
        output << "\n";
        // TODO
//        auto error_message = problem.display_user_friendly(solver, pool, DefaultSolvableDisplay());
//        return error_message.to_string();
        return "error message";
    } else {
        auto reason = provider.should_cancel_with_value();
        return reason.value();
    }
}

// Solve the problem and returns either a solution represented as a string or an error string.
std::string solve_snapshot(BundleBoxProvider &provider, const std::vector<std::string> &specs) {
    auto requirements = provider.requirements(specs);
    auto pool = provider.pool;
    auto solver = Solver<Range<Pack>, std::string, BundleBoxProvider>(provider);
    auto [steps, err] = solver.solve(requirements);
    if (err.has_value()) {
        auto problem = std::get<UnsolvableOrCancelled::Unsolvable>(err.value());
        std::stringstream output;
        output << "UNSOLVABLE:\n";
        return output.str();
    } else {
        auto reason = provider.should_cancel_with_value();
        return reason.value();
    }
}

#endif // SOLVER_H