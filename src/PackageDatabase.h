/**
 * Copyright Jerily LTD. All Rights Reserved.
 * SPDX-FileCopyrightText: 2024 Neofytos Dimitriou (neo@jerily.cy)
 * SPDX-License-Identifier: MIT.
 */

#ifndef TTREK_PACKAGE_DATABASE_H
#define TTREK_PACKAGE_DATABASE_H


#ifdef DEBUG
# define DBG(x) x
#else
# define DBG(x)
#endif

#include <satdepsolver.h>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>
#include <set>
#include <map>
#include <queue>
#include <unordered_set>
#include "semver/semver.h"
#include "Range.h"
#include "registry.h"
#include "cjson/cJSON.h"

inline std::vector<std::string_view> split_string(const std::string_view &str, const char *split_str) {
    std::vector<std::string_view> split;
    size_t start = 0;
    size_t end = str.find(split_str);
    while (end != std::string::npos) {
        split.push_back(std::string_view(str.substr(start, end - start)));
        start = end + 1;
        end = str.find(split_str, start);
    }
    split.push_back(std::string_view(str.substr(start, end)));
    return split;
}

struct UseFlag {
    std::string name;
    bool polarity;

    UseFlag(std::string_view use_flag_str) {
        bool polarity = true;
        if (use_flag_str.empty()) {
            throw std::runtime_error("Empty use flag");
        }
        if (use_flag_str[0] == '-') {
            polarity = false;
        } else if (use_flag_str[0] == '+') {
            polarity = true;
        } else {
            throw std::runtime_error("Invalid use flag: " + std::string(use_flag_str));
        }
        name = std::string(use_flag_str.substr(1));
        this->polarity = polarity;
    }

    UseFlag(std::string_view name, bool polarity) : name(std::string(name)), polarity(polarity) {}

    std::string to_string() const {
        return (polarity ? "+" : "-") + name;
    };

    bool operator==(const UseFlag &other) const {
        return name == other.name && polarity == other.polarity;
    }

    bool operator<(const UseFlag &other) const {
        return name < other.name || (name == other.name && polarity < other.polarity);
    }

};

// hash for UseFlag

namespace std {
    template<>
    struct hash<UseFlag> {
        std::size_t operator()(const UseFlag &use_flag) const {
            return std::hash<std::string>()(use_flag.to_string());
        }
    };
}

struct DependencyInfo {
    std::string version_requirement;
    std::unordered_set<UseFlag> if_use_flags;

    DependencyInfo(std::string version_requirement, std::string if_value) : version_requirement(std::move(version_requirement)) {
        if (!if_value.empty()) {
            auto use_flags = split_string(if_value, " ");
            for (const auto &use_flag : use_flags) {
                bool polarity = true;
                if (use_flag.empty()) { continue; }
                if (use_flag[0] == '-') {
                    polarity = false;
                } else if (use_flag[0] == '+') {
                    polarity = true;
                } else {
                    throw std::runtime_error("Invalid use flag: " + std::string(use_flag));
                }
                if_use_flags.emplace(UseFlag(use_flag.substr(1), polarity));
            }
        }
    }

};

using PackageVersions = std::map<std::string, std::vector<std::pair<std::string, DependencyInfo>>>;

inline PackageVersions fetch_package_versions(const std::string &package_name) {
    PackageVersions result;
    if (package_name.empty() || package_name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.+-") != std::string::npos) {
        throw std::runtime_error("Invalid package name: " + package_name);
    }
    const auto url = std::string(REGISTRY_URL) + "/" + package_name;
    Tcl_DString data;
    Tcl_DStringInit(&data);
    const auto status = ttrek_RegistryGet(url.c_str(), &data, nullptr);
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(Tcl_DStringValue(&data)), cJSON_Delete);
    Tcl_DStringFree(&data);
    if (status != TCL_OK || !cJSON_IsObject(root.get())) {
        throw std::runtime_error("Could not read registry metadata for " + package_name);
    }
    for (auto *version = root->child; version; version = version->next) {
        if (!cJSON_IsObject(version)) {
            throw std::runtime_error("Invalid dependency metadata for " + package_name);
        }
        auto &deps = result[version->string];
        for (auto *dep = version->child; dep; dep = dep->next) {
            const auto *range = cJSON_IsObject(dep) ? cJSON_GetObjectItem(dep, "version") : dep;
            const auto *condition = cJSON_IsObject(dep) ? cJSON_GetObjectItem(dep, "if") : nullptr;
            if (!cJSON_IsString(range) || (condition && !cJSON_IsString(condition))) {
                throw std::runtime_error("Invalid dependency " + std::string(dep->string) + " for " + package_name);
            }
            deps.emplace_back(dep->string, DependencyInfo(range->valuestring, condition ? condition->valuestring : ""));
        }
    }
    return result;
}

inline char EMPTY_STRING[] = "";

struct Pack {
private:
    // Copies held by the pool and version ranges share immutable parsed storage.
    std::shared_ptr<semver_t> parsed;
public:
    semver_t version{};

    explicit Pack(const std::string &version_str)
        : parsed(new semver_t{}, [](semver_t *value) { semver_free(value); delete value; }) {
        if (semver_parse(version_str.c_str(), parsed.get()) != 0) {
            throw std::runtime_error("Failed to parse version: " + version_str);
        }
        version = *parsed;
        if (!version.prerelease) {
            version.prerelease = EMPTY_STRING;
        }
    }

    Pack next_major_version() const {
        return Pack(std::to_string(version.major + 1) + ".0.0");
    }

    bool operator==(const Pack &other) const {
        return semver_compare(version, other.version) == 0;
    }

    bool operator<(const Pack &other) const {
        return semver_compare(version, other.version) < 0;
    }

    bool operator>(const Pack &other) const {
        return semver_compare(version, other.version) > 0;
    }

    bool operator<=(const Pack &other) const {
        return semver_compare(version, other.version) <= 0;
    }

    bool operator>=(const Pack &other) const {
        return semver_compare(version, other.version) >= 0;
    }

    std::string to_string() const {
        std::string version_str;
        version_str += std::to_string(version.major);
        version_str += ".";
        version_str += std::to_string(version.minor);
        version_str += ".";
        version_str += std::to_string(version.patch);
        if (version.prerelease != nullptr && version.prerelease[0] != '\0') {
            version_str += "-";
            version_str += version.prerelease;
        }
        return version_str;
    }

    friend std::string operator+(const std::string &lhs, const Pack &rhs) {
        return lhs + rhs.to_string();
    }

    friend std::string operator+(const Pack &lhs, const std::string &rhs) {
        return lhs.to_string() + rhs;
    }

    friend std::ostream &operator<<(std::ostream &os, const Pack &pack) {
        os << pack.to_string();
        return os;
    }

};

namespace std {
    template<> struct hash<Pack> {
        size_t operator()(const Pack &pack) const {
            // Equal versions may differ in build metadata or prerelease spelling.
            // Hash the numeric core; prereleases with the same core may collide.
            return hash<int>{}(pack.version.major) ^ (hash<int>{}(pack.version.minor) << 1)
                   ^ (hash<int>{}(pack.version.patch) << 2);
        }
    };
}

static std::pair<std::string_view, std::string_view> parse_operator(const std::string_view &s) {
    // >=, <=, >, <, =, ==
    if (s.size() >= 2 && s[0] == '>') {
        if (s[1] == '=') {
            return {">=", s.substr(2)};
        } else {
            return {">", s.substr(1)};
        }
    } else if (s.size() >= 2 && s[0] == '<') {
        if (s[1] == '=') {
            return {"<=", s.substr(2)};
        } else {
            return {"<", s.substr(1)};
        }
    } else if (s.size() >= 2 && s[0] == '=') {
        if (s[1] == '=') {
            return {"==", s.substr(2)};
        } else {
            return {"==", s.substr(1)};
        }
    } else if (s.size() >= 2 && s[0] == '^') {
        return {"^", s.substr(1)};
    } else {
        return {"==", s};
    }
}

static Range<Pack> version_range(std::optional<std::string_view> s) {
    if (s.has_value()) {
        std::string_view s_view = s.value();
        auto and_parts = split_string(s_view, ",");
        Range<Pack> and_range = Range<Pack>::full();
        for (const auto &and_part : and_parts) {
            const auto &[op, rest_view] = parse_operator(and_part);
            auto rest = std::string(rest_view);
            // >=, <=, >, <, =, ==
            if (op == ">=") {
                and_range = and_range.intersection_with(Range<Pack>::higher_than(Pack(rest)));
            } else if (op == ">") {
                and_range = and_range.intersection_with(Range<Pack>::strictly_higher_than(Pack(rest)));
            } else if (op == "<=") {
                and_range = and_range.intersection_with(Range<Pack>::lower_than(Pack(rest)));
            } else if (op == "<") {
                and_range = and_range.intersection_with(Range<Pack>::strictly_lower_than(Pack(rest)));
            } else if (op == "==") {
                and_range = and_range.intersection_with(Range<Pack>::singleton(Pack(rest)));
            } else if (op == "^") {
                and_range = and_range.intersection_with(Range<Pack>::compatible_with(Pack(rest)));
            } else {
                throw std::runtime_error("Invalid operator: " + std::string(op));
            }
        }
        return and_range;
    } else {
        return Range<Pack>::full();
    }
};

// ttrek owns semantic versions, registry access, USE flags, and install policy.
struct PackageDatabase : public DependencyProvider<Range<Pack>> {
    std::unordered_map<SolvableId, KnownDependencies> candidate_dependencies;
    std::set<std::string> candidate_names;
    std::map<std::string, std::unordered_set<std::string>> dependencies_map;
    std::map<std::string, std::unordered_set<std::string>> reverse_dependencies_map;
    std::unordered_set<UseFlag> global_use_flags;
    std::map<std::string, std::string> locked_packages;
    ttrek_strategy_t the_strategy = STRATEGY_LATEST;

    PackageDatabase() {
        pool = std::make_shared<Pool<Range<Pack>>>();
    }

    void set_strategy(ttrek_strategy_t strategy) { the_strategy = strategy; }

    void set_reverse_dependencies_map(const std::map<std::string, std::unordered_set<std::string>> &value) {
        reverse_dependencies_map = value;
    }

    const auto &get_dependencies_map() const { return dependencies_map; }

    std::unordered_set<std::string> get_reverse_dependencies_of_package(const std::string &name) const {
        auto it = reverse_dependencies_map.find(name);
        return it == reverse_dependencies_map.end() ? std::unordered_set<std::string>{} : it->second;
    }

    VersionSetId alloc_requirement_from_str(std::string_view name, std::string_view versions) {
        auto name_id = pool->intern_package_name(std::string(name));
        return pool->intern_version_set(name_id, version_range(versions.empty() ? std::nullopt : std::optional(versions)));
    }

    VersionSetId alloc_requirement_from_use_flag(const UseFlag &flag) {
        return alloc_requirement_from_str("use:" + flag.name, flag.polarity ? "1.2.3" : "0.0.0");
    }

    void alloc_locked_package(const std::string &name, const std::string &version) {
        locked_packages[name] = version;
    }

    SolvableId alloc_candidate(std::string_view name, const std::string &version, KnownDependencies dependencies) {
        auto name_id = pool->intern_package_name(std::string(name));
        auto id = pool->intern_solvable(name_id, Pack(version));
        candidate_dependencies.emplace(id, std::move(dependencies));
        candidate_names.insert(std::string(name));
        return id;
    }

    std::string display_solvable(SolvableId id) const {
        return DisplaySolvable(pool, pool->resolve_internal_solvable(id)).to_string();
    }

    std::optional<PackageCandidates> get_candidates(NameId name) override {
        auto package_name = pool->resolve_package_name(name);
        if (candidate_names.count(package_name) == 0 && package_name.rfind("use:", 0) != 0) {
            for (const auto &[version, deps] : fetch_package_versions(package_name)) {
                KnownDependencies dependencies;
                for (const auto &[dep_name, info] : deps) {
                    if (satisfies_use_flags(info.if_use_flags)) {
                        dependencies.requirements.push_back(alloc_requirement_from_str(dep_name, info.version_requirement));
                    }
                }
                alloc_candidate(package_name, version, std::move(dependencies));
            }
        }
        PackageCandidates result;
        for (const auto &[id, dependencies] : candidate_dependencies) {
            auto candidate = pool->resolve_solvable(id);
            if (!(candidate.get_name_id() == name)) { continue; }
            result.candidates.push_back(id);
            result.hint_dependencies_available.push_back(id);
            auto locked = locked_packages.find(package_name);
            if (locked != locked_packages.end() && candidate.get_inner().to_string() == locked->second) {
                if (the_strategy == STRATEGY_LOCKED) { result.locked = id; }
                if (the_strategy == STRATEGY_FAVORED) { result.favored = id; }
            }
        }
        return result;
    }

    void sort_candidates(std::vector<SolvableId> &ids) override {
        std::sort(ids.begin(), ids.end(), [this](SolvableId a, SolvableId b) {
            return pool->resolve_solvable(a).get_inner() > pool->resolve_solvable(b).get_inner();
        });
    }

    DependenciesVariant get_dependencies(SolvableId id) override {
        return Dependencies::Known{candidate_dependencies.at(id)};
    }

    std::optional<std::string> should_cancel_with_value() override { return std::nullopt; }

    void record_selected_dependencies(const std::vector<SolvableId> &selected) {
        dependencies_map.clear();
        for (auto id : selected) {
            auto name = pool->resolve_package_name(pool->resolve_solvable(id).get_name_id());
            auto &deps = dependencies_map[name];
            for (const auto &requirement : candidate_dependencies.at(id).requirements) {
                for (auto version_set : requirement.version_sets(pool)) {
                    deps.insert(pool->resolve_package_name(pool->resolve_version_set_package_name(version_set)));
                }
            }
        }
    }

    void topological_sort(std::vector<std::string> &installs) {
        std::map<std::string, std::set<std::string>> graph;
        std::map<std::string, int> indegree;
        std::set<std::string> visited;
        std::vector<std::string> result;
        for (const auto &install : installs) {
            auto package_name = install.substr(0, install.find('='));
            auto package_version = install.substr(install.find('=') + 1);

            auto package_deps = dependencies_map[package_name];
            for (const auto &dep : package_deps) {
                graph[dep].insert(package_name);
                indegree[package_name]++;
            }
        }
        std::queue<std::string> q;
        for (const auto &install : installs) {
            auto package_name = install.substr(0, install.find('='));
            if (indegree[package_name] == 0) {
                q.push(package_name);
            }
        }
        while (!q.empty()) {
            auto package_name = q.front();
            q.pop();
            result.push_back(package_name);
            for (const auto &dep : graph[package_name]) {
                indegree[dep]--;
                if (indegree[dep] == 0) {
                    q.push(dep);
                }
            }
        }

//        std::cout << "--------------------------- Topological sort:" << std::endl;
//        for (const auto &package_name : result) {
//            std::cout << package_name << std::endl;
//        }
//        std::cout << "---------------------------" << std::endl;

        // now sort the installs based on the topological sort
        std::map<std::string, uint32_t> package_to_index;
        for (uint32_t i = 0; i < result.size(); i++) {
            package_to_index[result[i]] = i;
        }

        std::sort(installs.begin(), installs.end(), [&package_to_index](const std::string &a, const std::string &b) {
            return package_to_index[a.substr(0, a.find('='))] < package_to_index[b.substr(0, b.find('='))];
        });
    }

    void set_global_use_flags(std::unordered_set<UseFlag> use_flags) {
        global_use_flags.insert(use_flags.begin(), use_flags.end());
    }

    bool satisfies_use_flags(const std::unordered_set<UseFlag> &use_flags) {
        for (const auto &use_flag : use_flags) {
            if (global_use_flags.find(use_flag) == global_use_flags.end()) {
                return false;
            }
        }
        return true;
    }
};

// The solver owns a provider copy. Return its populated metadata to the installer
// after formatting conflicts, while all cached IDs still belong to this pool.
inline std::string solve_packages(PackageDatabase &db, const std::vector<VersionSetId> &requirements,
                                  const std::vector<VersionSetId> &constraints, std::vector<SolvableId> &result) {
    Solver<Range<Pack>, std::string, PackageDatabase> solver(db);
    auto [selected, error] = solver.solve(Problem().requirements(requirements).constraints(constraints));
    result.clear();
    std::string message;
    if (error) {
        if (auto *unsolvable = std::get_if<UnsolvableOrCancelled::Unsolvable>(&*error)) {
            auto graph = solver.graph(unsolvable->conflict);
            message = graph ? DisplayUnsat<Range<Pack>, std::string>(db.pool, *graph).to_string()
                            : "Dependency resolution failed";
        } else {
            message = "Dependency resolution cancelled";
        }
    } else {
        result = std::move(selected);
    }
    db = std::move(solver.cache.provider);
    db.record_selected_dependencies(result);
    return message;
}

#endif //TTREK_PACKAGE_DATABASE_H