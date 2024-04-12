#ifndef SOLVER_H
#define SOLVER_H

#include "../internal/NameId.h"
#include "../internal/VersionSetId.h"
#include "../Common.h"
#include "../Range.h"
#include "../Pool.h"
#include <cstdint>
#include <unordered_set>

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

    Pack(uint32_t version, bool unknown_deps = false, bool cancel_during_get_dependencies = false)
        : version(version), unknown_deps(unknown_deps), cancel_during_get_dependencies(cancel_during_get_dependencies) {}

    Pack with_unknown_deps() const {
        return Pack(version, true, cancel_during_get_dependencies);
    }

    Pack with_cancel_during_get_dependencies() const {
        return Pack(version, unknown_deps, true);
    }

    Pack offset(uint32_t offset) const {
        return Pack(version + offset, unknown_deps, cancel_during_get_dependencies);
    }

    bool operator==(const Pack &other) const {
        return version == other.version;
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

        auto spec_versions = version_range(split[1]);
        return Spec{spec_name, spec_versions};
    }
};

//struct BundleBoxProvider {
//    pool: Rc<Pool<Range<Pack>>>,
//    packages: IndexMap<String, IndexMap<Pack, BundleBoxPackageDependencies>>,
//    favored: HashMap<String, Pack>,
//    locked: HashMap<String, Pack>,
//    excluded: HashMap<String, HashMap<Pack, String>>,
//    cancel_solving: Cell<bool>,
//    // TODO: simplify?
//    concurrent_requests: Arc<AtomicUsize>,
//    concurrent_requests_max: Rc<Cell<usize>>,
//    sleep_before_return: bool,
//
//    // A mapping of packages that we have requested candidates for. This way we can keep track of duplicate requests.
//    requested_candidates: RefCell<HashSet<NameId>>,
//    requested_dependencies: RefCell<HashSet<SolvableId>>,
//}
// in c++ it  should be:

class BundleBoxProvider {
public:

    Pool<Range<Pack>> pool;
    std::unordered_map<std::string, std::unordered_map<Pack, KnownDependencies>> packages;
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
        for (const auto &dep : requirements) {
            auto spec = Spec::from_str(dep);
            auto dep_name = pool.intern_package_name(spec.name);
            version_set_ids.push_back(pool.intern_version_set(dep_name, spec.versions));
        }
        return version_set_ids;
    }

    void from_packages(const std::vector<std::tuple<std::string, uint32_t, std::vector<std::string>>> &packages) {
        for (const auto &package : packages) {
            auto name = std::get<0>(package);
            auto version = std::get<1>(package);
            auto deps = std::get<2>(package);
            add_package(name, Pack(version), deps, {});
        }
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

    void add_package(const std::string &package_name, Pack package_version, const std::vector<std::string> &package_dependencies, const std::vector<std::string> &package_constrains) {
        std::vector<Spec> deps;
        for (const auto &dep : package_dependencies) {
            deps.push_back(Spec::from_str(dep));
        }

        std::vector<Spec> cons;
        for (const auto &con : package_constrains) {
            cons.push_back(Spec::from_str(con));
        }

        packages.at(package_name).insert(std::make_pair(package_version, BundleBoxPackageDependencies{deps, cons}));
    }

private:
    std::vector<Spec> dependencies;
    std::vector<Spec> constrains;
};

#endif // SOLVER_H