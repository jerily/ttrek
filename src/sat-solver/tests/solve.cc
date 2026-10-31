#include "../../PackageDatabase.h"
#include <fstream>
#include <iterator>

// The registry fixture uses the same version -> dependencies shape as ttrek.sh.
static std::unique_ptr<cJSON, decltype(&cJSON_Delete)> registry(nullptr, cJSON_Delete);

extern "C" int ttrek_RegistryGet(const char *url, Tcl_DString *data, cJSON *) {
    auto name = std::string(url).substr(std::string(url).find_last_of('/') + 1);
    auto *package = cJSON_GetObjectItemCaseSensitive(registry.get(), name.c_str());
    if (!package) { return TCL_ERROR; }
    auto *json = cJSON_PrintUnformatted(package);
    Tcl_DStringAppend(data, json, -1);
    cJSON_free(json);
    return TCL_OK;
}

static std::vector<std::string> solve(PackageDatabase &db, const std::vector<VersionSetId> &requirements,
                                     const std::vector<VersionSetId> &constraints = {}) {
    std::vector<SolvableId> result;
    auto message = solve_packages(db, requirements, constraints, result);
    if (!message.empty()) { throw std::runtime_error(message); }
    std::vector<std::string> installs;
    for (auto id : result) { installs.push_back(db.display_solvable(id)); }
    db.topological_sort(installs);
    return installs;
}

static void test_registry_backtracking_and_order() {
    PackageDatabase db;
    auto result = solve(db, {db.alloc_requirement_from_str("app", "")});
    assert((result == std::vector<std::string>{"lib=1.2.0", "app=1.0.0"}));
    // Dependencies of rejected app versions must not enter the installation graph.
    assert(db.get_dependencies_map().at("app").count("missing") == 0);
}

static void test_constraints() {
    PackageDatabase db;
    auto result = solve(db, {db.alloc_requirement_from_str("app", "")},
                        {db.alloc_requirement_from_str("lib", "<1.2.0"),
                         db.alloc_requirement_from_str("missing", "1.0.0")});
    assert((result == std::vector<std::string>{"lib=1.0.0", "app=1.0.0"}));
}

static void test_strategies() {
    for (auto strategy : {STRATEGY_LATEST, STRATEGY_FAVORED, STRATEGY_LOCKED}) {
        PackageDatabase db;
        db.set_strategy(strategy);
        db.alloc_locked_package("lib", "1.0.0");
        auto result = solve(db, {db.alloc_requirement_from_str("lib", "")});
        assert(result.front() == (strategy == STRATEGY_LATEST ? "lib=2.0.0" : "lib=1.0.0"));
    }
    PackageDatabase favored;
    favored.set_strategy(STRATEGY_FAVORED);
    favored.alloc_locked_package("lib", "1.0.0");
    assert(solve(favored, {favored.alloc_requirement_from_str("lib", ">=1.2.0")}).front() == "lib=2.0.0");

    PackageDatabase locked;
    locked.set_strategy(STRATEGY_LOCKED);
    locked.alloc_locked_package("lib", "1.0.0");
    std::vector<SolvableId> result;
    auto message = solve_packages(locked, {locked.alloc_requirement_from_str("lib", ">=1.2.0")}, {}, result);
    assert(!message.empty());
    assert(result.empty());
}

static void test_use_flags() {
    PackageDatabase disabled;
    auto without = solve(disabled, {disabled.alloc_requirement_from_str("feature", "")});
    assert((without == std::vector<std::string>{"feature=1.0.0"}));

    PackageDatabase enabled;
    enabled.set_global_use_flags({UseFlag("+threads"), UseFlag("-debug")});
    auto with = solve(enabled, {enabled.alloc_requirement_from_str("feature", "")});
    assert((with == std::vector<std::string>{"lib=1.2.0", "feature=1.0.0"}));

    PackageDatabase flags;
    flags.alloc_candidate("use:threads", "0.0.0", {});
    flags.alloc_candidate("use:threads", "1.2.3", {});
    assert(solve(flags, {flags.alloc_requirement_from_use_flag(UseFlag("+threads"))}).front() == "use:threads=1.2.3");
}

static void test_errors() {
    PackageDatabase db;
    std::vector<SolvableId> result;
    auto message = solve_packages(db, {db.alloc_requirement_from_str("missing", "")}, {}, result);
    assert(result.empty());
    assert(message.find("missing") != std::string::npos);
    for (const auto *name : {"broken", "transport-failure"}) {
        PackageDatabase invalid;
        bool threw = false;
        try { solve(invalid, {invalid.alloc_requirement_from_str(name, "")}); }
        catch (const std::runtime_error &) { threw = true; }
        assert(threw);
    }
}

static void test_version_ownership_and_hashing() {
    Pack prerelease("9.0.0-beta.2");
    auto copy = prerelease;
    prerelease = Pack("9.1.0");
    assert(copy.to_string() == "9.0.0-beta.2");
    Pack first("1.2.3+one"), second("1.2.3+two");
    assert(first == second);
    assert(std::hash<Pack>{}(first) == std::hash<Pack>{}(second));
    assert(!version_range("<9.0.0").contains(copy)); // existing ttrek prerelease policy
    assert(version_range(">=8.6.14,<9.0.0").contains(Pack("8.6.15")));
}

int main() {
    std::ifstream file(SOLVER_FIXTURE_PATH);
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    registry.reset(cJSON_Parse(json.c_str()));
    assert(registry);
    test_registry_backtracking_and_order();
    test_constraints();
    test_strategies();
    test_use_flags();
    test_errors();
    test_version_ownership_and_hashing();
    return 0;
}
