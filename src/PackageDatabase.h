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

#include <resolvo.h>
#include <resolvo_pool.h>
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

std::vector<resolvo::String> split_string(const std::string_view &str, const char *split_str) {
    std::vector<resolvo::String> split;
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

std::map<std::string_view, std::vector<std::pair<std::string_view, std::string_view>>> fetch_package_versions(const std::string& package_name) {
    std::map<std::string_view, std::vector<std::pair<std::string_view, std::string_view>>> result;
    char package_versions_url[256];
    snprintf(package_versions_url, sizeof(package_versions_url), "%s/%s", REGISTRY_URL, package_name.c_str());
    Tcl_DString versions_ds;
    Tcl_DStringInit(&versions_ds);
    if (TCL_OK != ttrek_RegistryGet(package_versions_url, &versions_ds, NULL)) {
        fprintf(stderr, "error: could not get versions for %s\n", package_name.c_str());
        return result;
    }
    cJSON *versions_root = cJSON_Parse(Tcl_DStringValue(&versions_ds));
    Tcl_DStringFree(&versions_ds);
    for (int i = 0; i < cJSON_GetArraySize(versions_root); i++) {
        cJSON *version_item = cJSON_GetArrayItem(versions_root, i);
        const char *version_str = version_item->string;
        DBG(fprintf(stderr, "version_str: %s\n", version_str));
        std::vector<std::pair<std::string_view, std::string_view>> deps;
        for (int j = 0; j < cJSON_GetArraySize(version_item); j++) {
            cJSON *deps_item = cJSON_GetArrayItem(version_item, j);
            const char *dep_name = deps_item->string;
            const char *dep_version = cJSON_GetStringValue(deps_item);
            DBG(fprintf(stderr, "dep_name: %s, dep_version: %s\n", dep_name, dep_version));
            deps.emplace_back(dep_name, dep_version);
        }
        result[version_str] = deps;
    }
    cJSON_free(versions_root);
    return result;
}

static char EMPTY_STRING[] = "";

struct Pack {
    semver_t version = {0};

    explicit Pack(const std::string& version_str) {
        if (-1 == semver_parse(version_str.c_str(), &version)) {
            throw std::runtime_error("Failed to parse version: " + version_str);
        }
        if (version.prerelease == nullptr) {
            version.prerelease = EMPTY_STRING;
        }
    }

    explicit Pack(const semver_t &version) : version(version) {}

    Pack next_major_version() const {
        semver_t next_version = version;
        next_version.major++;
        next_version.minor = 0;
        next_version.patch = 0;

        // Needed to make sure 9.0.0-beta.2 does NOT satisfy <9.0.0
        // in essence the empty string will make sure that the prerelease
        // precedes any other alpha, beta, and so on versions.
        next_version.prerelease = EMPTY_STRING;
        return Pack(next_version);
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

/**
 * A single candidate for a package.
 */
struct Candidate {
    resolvo::NameId name;
    Pack version;
    resolvo::Dependencies dependencies;
    resolvo::SolvableId id;
};

/**
 * A requirement for a package.
 */

struct Requirement {
    resolvo::NameId name;
//    uint32_t version_start;
//    uint32_t version_end;
    Range<Pack> versions;
};

struct ReverseDependency {
    std::string package_name;
    std::string package_version;
};

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

static Range<Pack> version_range(std::optional<resolvo::String> s) {
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

/**
 * A simple database of packages that also implements resolvos DependencyProvider interface.
 */
struct PackageDatabase : public resolvo::DependencyProvider {
    resolvo::Pool<resolvo::NameId, resolvo::String> names;
    resolvo::Pool<resolvo::StringId, resolvo::String> strings;
    std::vector<Candidate> candidates;
    std::vector<Requirement> requirements;

    std::set<std::string> candidate_names;
    std::map<std::string, std::unordered_set<std::string>> dependencies_map;
    std::map<std::string, std::unordered_set<std::string>> reverse_dependencies_map;
    std::map<std::string, std::string> locked_packages;
    ttrek_strategy_t the_strategy;

    void set_strategy(ttrek_strategy_t strategy) {
        the_strategy = strategy;
    }

    void set_reverse_dependencies_map(const std::map<std::string, std::unordered_set<std::string>> &revdependencies_map) {
        reverse_dependencies_map = revdependencies_map;
    }

    const std::map<std::string, std::unordered_set<std::string>>& get_dependencies_map() {
        return dependencies_map;
    }

    const std::unordered_set<std::string> get_reverse_dependencies_of_package(const std::string &package_name) {
        if (reverse_dependencies_map.find(package_name) != reverse_dependencies_map.end()) {
            return reverse_dependencies_map.at(package_name);
        }
        return std::unordered_set<std::string>();
    }

    /**
     * Allocates a new requirement and return the id of the requirement.
     */
    resolvo::VersionSetId alloc_requirement_from_str(const std::string_view &package_name, const std::string_view &package_versions) {
        auto spec_name = names.alloc(package_name);
        auto spec_versions = version_range(package_versions.empty() ? std::nullopt : std::optional(package_versions));
        auto requirement = Requirement{spec_name, spec_versions};
        auto id = resolvo::VersionSetId{static_cast<uint32_t>(requirements.size())};
        requirements.push_back(requirement);
        return id;
    }

    void alloc_locked_package(const std::string &package_name, const std::string &package_version) {
        DBG(std::cout << "set locked package: " << package_name << "=" << package_version << std::endl);
        locked_packages[package_name] = package_version;
    }

    /**
     * Allocates a new candidate and return the id of the candidate.
     */
    resolvo::SolvableId alloc_candidate(std::string_view name, const std::string& version,
                                        resolvo::Dependencies dependencies) {
        // check if the candidate already exists
        auto package_name = std::string(name);
        auto install = package_name + "=" + version;
//        if (candidate_to_solvable_map.find(install) != candidate_to_solvable_map.end()) {
//            return candidate_to_solvable_map.at(install);
//        }

        auto name_id = names.alloc(name);
        auto id = resolvo::SolvableId{static_cast<uint32_t>(candidates.size())};
        candidates.push_back(Candidate{name_id, Pack(version), std::move(dependencies), id});
//        candidate_to_solvable_map[install] = id;
        if (candidate_names.find(package_name) == candidate_names.end()) {
            candidate_names.insert(package_name);
        }
        return id;
    }

    resolvo::String display_name(resolvo::NameId name) override {
        return resolvo::String(names[name]);
    }

    resolvo::String display_solvable(resolvo::SolvableId solvable) override {
        const auto& candidate = candidates[solvable.id];
        std::stringstream ss;
        ss << names[candidate.name] << "=" << candidate.version;
        return resolvo::String(ss.str());
    }

    resolvo::String display_merged_solvables(
            resolvo::Slice<resolvo::SolvableId> solvables) override {
        if (solvables.empty()) {
            return resolvo::String();
        }

        std::stringstream ss;
        ss << names[candidates[solvables[0].id].name] << " ";

        bool first = true;
        for (const auto& solvable : solvables) {
            if (!first) {
                ss << " | ";
            }

            ss << candidates[solvable.id].version;
            first = false;
        }

        return resolvo::String(ss.str());
    }

    resolvo::String display_version_set(resolvo::VersionSetId version_set) override {
        const auto& req = requirements[version_set.id];
        std::stringstream ss;
//        ss << req.version_start << ".." << req.version_end;
        ss << req.versions;
        return resolvo::String(ss.str());
    }

    resolvo::String display_string(resolvo::StringId string_id) override {
        return strings[string_id];
    }

    resolvo::NameId version_set_name(resolvo::VersionSetId version_set_id) override {
        return requirements[version_set_id.id].name;
    }

    resolvo::NameId solvable_name(resolvo::SolvableId solvable_id) override {
        return candidates[solvable_id.id].name;
    }

    resolvo::Candidates get_candidates(resolvo::NameId package) override {
        auto package_name = std::string(names[package]);
        auto set_locked_p = locked_packages.find(package_name) != locked_packages.end();
        DBG(std::cout << "package: " << package_name << " set_locked_p = " << set_locked_p << std::endl);
        resolvo::SolvableId locked_candidate_id{};
//        std::cout << "package=" << names[package] << std::endl;
        if (candidate_names.find(package_name) == candidate_names.end()) {
            DBG(std::cout << "fetching from remote: " << names[package] << std::endl);
            dependencies_map[package_name] = std::unordered_set<std::string>();
            auto package_versions = fetch_package_versions(package_name);
            for (const auto & it : package_versions) {
                auto package_version = std::string(it.first);
                auto package_version_deps = it.second;

                auto dependencies = resolvo::Dependencies();

                // add the dependencies for the package
                for (const auto &dep : package_version_deps) {
                    auto dep_version_set = alloc_requirement_from_str(dep.first, dep.second);
                    dependencies.requirements.push_back(dep_version_set);
                    DBG(std::cout << "dependency for " << package_name << ": " << dep.first << "@" << dep.second << std::endl);

                    // keep track of the dependencies for each package
                    // so that we can later sort the installs based on
                    // the topological sort of the dependencies
                    dependencies_map[package_name].insert(std::string(dep.first));
                }

                auto id = alloc_candidate(package_name, package_version, dependencies);
                DBG(std::cout << "candidate: " << package_name << "=" << package_version << std::endl);
                if (set_locked_p && locked_packages[package_name] == package_version) {
                    DBG(std::cout << "locked package: " << package_name << "=" << package_version << std::endl);
                    locked_candidate_id = id;
                }
            }
        }

        resolvo::Candidates result;
        result.favored = nullptr;
        result.locked = nullptr;

        for (uint32_t i = 0; i < static_cast<uint32_t>(candidates.size()); ++i) {
            const auto& candidate = candidates[i];
            if (candidate.name != package) {
                continue;
            }

            if (set_locked_p && locked_candidate_id == candidate.id) {
                if (the_strategy == STRATEGY_LOCKED) {
                    result.locked = &candidate.id;
                } else if (the_strategy == STRATEGY_FAVORED) {
                    result.favored = &candidate.id;
                } else {
                    // do nothing
                }
            }

            result.candidates.push_back(resolvo::SolvableId{i});
            result.hint_dependencies_available.push_back(resolvo::SolvableId{i});
        }
        DBG(std::cout << result.candidates.size() << " candidates for " << names[package] << std::endl);
        return result;
    }

    void sort_candidates(resolvo::Slice<resolvo::SolvableId> solvables) override {
        std::sort(solvables.begin(), solvables.end(),
                  [&](resolvo::SolvableId a, resolvo::SolvableId b) {
                      return candidates[a.id].version > candidates[b.id].version;
                  });
    }

    resolvo::Vector<resolvo::SolvableId> filter_candidates(
            resolvo::Slice<resolvo::SolvableId> solvables, resolvo::VersionSetId version_set_id,
            bool inverse) override {
        resolvo::Vector<resolvo::SolvableId> result;
        const auto& requirement = requirements[version_set_id.id];
        for (auto solvable : solvables) {
            const auto& candidate = candidates[solvable.id];
            bool matches = requirement.versions.contains(candidate.version);
            if (matches != inverse) {
                result.push_back(solvable);
            }
        }
        return result;
    }

    resolvo::Dependencies get_dependencies(resolvo::SolvableId solvable) override {
        const auto& candidate = candidates[solvable.id];
        return candidate.dependencies;
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
};

#endif //TTREK_PACKAGE_DATABASE_H