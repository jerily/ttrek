#ifndef TTREK_REGISTRY_BATCH_H
#define TTREK_REGISTRY_BATCH_H

#include <map>
#include <set>
#include <string>
#include <vector>

struct RegistryPlanItem {
    std::string name;
    std::string version;
    bool recipe;
};

std::string ttrek_MissingPackage(const std::string &name);

// Keys absent from the result were missing in every registry.
std::map<std::string, std::string> ttrek_RegistryPackageBatch(const std::set<std::string> &names);
void ttrek_RegistryPlanBatch(const std::vector<RegistryPlanItem> &items, const char *os, const char *arch);

#endif
