#ifndef DISPLAY_UNSAT_H
#define DISPLAY_UNSAT_H

#include <sstream>
#include "Pool.h"
#include "DisplaySolvable.h"
#include "DisplayMergedSolvable.h"
#include "Problem.h"

namespace DisplayOp {
    struct Requirement {
        VersionSetId version_set_id;
        std::vector<EdgeIndex> edges;
    };
    struct Candidate {
        NodeIndex node_index;
    };
}

using DisplayOpVariant = std::variant<DisplayOp::Requirement, DisplayOp::Candidate>;

template<typename VS, typename N>
class DisplayUnsat {
private:
    std::shared_ptr<Pool<VS, N>> pool;
    ProblemGraph graph;
    std::unordered_map<SolvableId, MergedProblemNode> merged_candidates;
    std::unordered_set<NodeIndex> installable_set;
    std::unordered_set<NodeIndex> missing_set;

public:
// Constructor
    explicit DisplayUnsat(std::shared_ptr<Pool<VS, N>> poolRef, const ProblemGraph &graphRef,
                           const std::unordered_map<SolvableId, MergedProblemNode> &merged_candidatesRef,
                           const std::unordered_set<NodeIndex> &installable_setRef,
                           const std::unordered_set<NodeIndex> &missing_setRef)
            : pool(poolRef), graph(graphRef) {
        merged_candidates = graph.simplify(pool);
        installable_set = graph.get_installable_set();
        missing_set = graph.get_missing_set();
    }

    friend std::ostream &operator<<(std::ostream &os, const DisplayUnsat &display_unsat) {
        os << display_unsat.to_string();
        return os;
    }

    // toString method
    std::string to_string() {
        std::ostringstream oss;

        auto top_level_edges = graph.graph.outgoing_edges(graph.root_node);
        auto top_level_missing = std::copy_if(top_level_edges, [this](auto &edge) {
            return missing_set.find(edge.get_node_to().get_id()) != missing_set.end();
        });
        auto top_level_conflicts = std::copy_if(top_level_edges, [this](auto &edge) {
            return std::any_of(graph.graph.outgoing_edges(edge.get_node_to()).begin(), graph.graph.outgoing_edges(edge.get_node_to()).end(), [](auto &edge) {
                return std::holds_alternative<ProblemEdge::Conflict>(edge.get_weight());
            });
        });

        if (!top_level_missing.empty()) {
            return fmt_graph(oss, top_level_missing, false);
        }

        if (!top_level_conflicts.empty()) {
            oss << "The following packages are incompatible" << std::endl;
            fmt_graph(oss, top_level_conflicts, true);

            auto edges = graph.graph.outgoing_edges(graph.root_node);
            auto indenter = Indenter(true);

            for (auto &edge : edges) {
                auto child_order = std::holds_alternative<ProblemEdge::Conflict>(edge.get_weight()) ? ChildOrder::HasRemainingSiblings : ChildOrder::Last;
                auto temp_indenter = indenter.push_level_with_order(child_order);
                auto indent = temp_indenter.get_indent();

                //                let conflict = match e.weight() {
                //                    ProblemEdge::Requires(_) => continue,
                //                    ProblemEdge::Conflict(conflict) => conflict,
                //                };

                if (!std::holds_alternative<ProblemEdge::Conflict>(edge.get_weight())) {
                    continue;
                }

                auto conflict = std::get<ProblemEdge::Conflict>(edge.get_weight()).conflict_cause;

                if (std::holds_alternative<ConflictCause::Constrains>(conflict) || std::holds_alternative<ConflictCause::ForbidMultipleInstances>(conflict)) {
                    throw std::runtime_error("Unreachable");
                } else if (std::holds_alternative<ConflictCause::Locked>(conflict)) {
                    auto locked = pool->resolve_solvable(std::get<ConflictCause::Locked>(conflict).solvable);
                    auto display_merged_solvable = DisplayMergedSolvable<VS, N>(pool, std::get<ConflictCause::Locked>(conflict).solvable);
                    oss << indent << locked.get_name() << " " << display_merged_solvable.to_string() << " is locked, but another version is required as reported above" << std::endl;
                } else if (std::holds_alternative<ConflictCause::Excluded>(conflict)) {
                    continue;
                }
            }
        }

        return oss.str();
    }

    std::string fmt_graph(std::ostringstream& oss, std::unordered_set<Edge<ProblemNodeVariant, ProblemEdgeVariant>> top_level_edges, bool top_level_indent) const {

        auto reported = std::unordered_set<SolvableId>();
        // Note: we are only interested in requires edges here
        auto indenter = Indenter(top_level_indent);

        auto requires_edges = std::copy_if(top_level_edges, [](auto &edge) {
            return std::holds_alternative<ProblemEdge::Requires>(edge.get_weight());
        });

        std::unordered_map<VersionSetId, std::vector<Edge<ProblemNodeVariant, ProblemEdgeVariant>>> chunked;
        for (auto &edge : requires_edges) {
            auto requires = std::get<ProblemEdge::Requires>(edge.get_weight());
            chunked[requires.version_set_id].push_back(edge);
        }

        std::sort(chunked.begin(), chunked.end(), [this](auto &a, auto &b) {
            auto a_edges = a.second;
            auto b_edges = b.second;
            auto a_installable = std::any_of(a_edges.begin(), a_edges.end(), [this](auto &edge) {
                return installable_set.find(edge.get_node_to().get_id()) != installable_set.end();
            });
            auto b_installable = std::any_of(b_edges.begin(), b_edges.end(), [this](auto &edge) {
                return installable_set.find(edge.get_node_to().get_id()) != installable_set.end();
            });
            return a_installable > b_installable;
        });

        std::vector<std::pair<DisplayOpVariant, Indenter>> stack = std::map(chunked.begin(), chunked.end(), [&indenter](auto &pair) {
            // pair.first is version_set_id
            // pair.second is vector of edges
            return std::make_pair(DisplayOp::Requirement{pair.first, pair.second}, indenter.push_level());
        });

        if (!stack.empty()) {
            // Mark the first element of the stack as not having any remaining siblings
            stack[0].second.set_last();
        }

        while (!stack.empty()) {
            auto node_indenter_pair = stack.back();
            stack.pop_back();
            auto top_level = node_indenter_pair.second.is_at_top_level();
            auto indent = node_indenter_pair.second.get_indent();

            std::visit([this, &oss, &reported, &stack, &node_indenter_pair, &top_level, &indent](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, DisplayOp::Requirement>) {
                    auto requirement = std::any_cast<DisplayOp::Requirement>(arg);
                    auto version_set_id = requirement.version_set_id;
                    auto edges = requirement.edges;
                    assert(!edges.empty());

                    auto installable = std::any_of(edges.begin(), edges.end(), [this](auto &edge) {
                        return installable_set.find(edge.get_node_to().get_id()) != installable_set.end();
                    });

                    auto req = pool->resolve_version_set(version_set_id);
                    auto name = pool->resolve_version_set_package_name(version_set_id);
                    name = pool->resolve_package_name(name);
                    auto target = edges[0].get_node_to();
                    auto missing = edges.size() == 1 && std::holds_alternative<ProblemNode::UnresolvedDependency>(target.get_payload());

                    if (missing) {
                        // No candidates for requirement
                        if (top_level) {
                            oss << indent << "No candidates were found for " << name << " " << req << "." << std::endl;
                        } else {
                            oss << indent << name << " " << req << ", for which no candidates were found." << std::endl;
                        }
                    } else if (installable) {
                        // Package can be installed (only mentioned for top-level requirements)
                        if (top_level) {
                            oss << indent << name << " " << req << " can be installed with any of the following options:" << std::endl;
                        } else {
                            oss << indent << name << " " << req << ", which can be installed with any of the following options:" << std::endl;
                        }

                        auto filtered_edges = std::copy_if(edges, [this](auto &edge) {
                            return installable_set.find(edge.get_node_to().get_id()) != installable_set.end();
                        });
                        std::vector<std::pair<DisplayOpVariant, Indenter>> children = std::map(filtered_edges.begin(), filtered_edges.end(), [&node_indenter_pair](auto &edge) {
                            return std::make_pair(DisplayOp::Candidate{edge.get_node_to().get_id()}, node_indenter_pair.second.push_level());
                        });

                        std::vector<std::pair<DisplayOpVariant, Indenter>> deduplicated_children;
                        std::unordered_set<SolvableId> merged_and_seen;
                        for (auto &child : children) {
                            if (!std::holds_alternative<DisplayOp::Candidate>(child.first)) {
                                throw std::runtime_error("Unexpected child type");
                            }
                            auto child_node = std::get<DisplayOp::Candidate>(child.first);
                            auto child_node_index = child_node.node_index;
                            auto payload = graph.graph.get_node(child_node_index).get_payload();
                            auto solvable_id = std::get<ProblemNode::Solvable>(payload).solvable;
                            auto merged = merged_candidates.find(solvable_id);

                            if (merged_and_seen.find(solvable_id) != merged_and_seen.end()) {
                                continue;
                            }

                            if (merged != merged_candidates.end()) {
                                for (auto &id : merged->second.ids) {
                                    merged_and_seen.insert(id);
                                }
                            }

                            deduplicated_children.push_back(child);
                        }

                        if (!deduplicated_children.empty()) {
                            // Mark the first element of the stack as not having any remaining siblings
                            deduplicated_children[0].second.set_last();
                        }

                        stack.insert(stack.end(), deduplicated_children.begin(), deduplicated_children.end());

                    } else {
                        // Package cannot be installed (the conflicting requirement is further down the tree)
                        if (top_level) {
                            oss << indent << name << " " << req << " cannot be installed because there are no viable options:" << std::endl;
                        } else {
                            oss << indent << name << " " << req
                                << ", which cannot be installed because there are no viable options:" << std::endl;
                        }


                        auto filtered_edges = std::copy_if(edges, [this](auto &edge) {
                            return missing_set.find(edge.get_node_to().get_id()) != missing_set.end();
                        });

                        std::vector<std::pair<DisplayOpVariant, Indenter>> children = std::map(filtered_edges.begin(), filtered_edges.end(), [&node_indenter_pair](auto &edge) {
                            return std::make_pair(DisplayOp::Candidate{edge.get_node_to().get_id()}, node_indenter_pair.second.push_level());
                        });

                        std::vector<std::pair<DisplayOpVariant, Indenter>> deduplicated_children;
                        std::unordered_set<SolvableId> merged_and_seen;

                        for (auto &child : children) {
                            if (!std::holds_alternative<DisplayOp::Candidate>(child.first)) {
                                throw std::runtime_error("Unexpected child type");
                            }
                            auto child_node = std::get<DisplayOp::Candidate>(child.first);
                            auto child_node_index = child_node.node_index;
                            auto payload = graph.graph.get_node(child_node_index).get_payload();
                            auto solvable_id = std::get<ProblemNode::Solvable>(payload).solvable;
                            auto merged = merged_candidates.find(solvable_id);

                            if (merged_and_seen.find(solvable_id) != merged_and_seen.end()) {
                                continue;
                            }

                            if (merged != merged_candidates.end()) {
                                for (auto &id : merged->second.ids) {
                                    merged_and_seen.insert(id);
                                }
                            }

                            deduplicated_children.push_back(child);
                        }

                        if (!deduplicated_children.empty()) {
                            // Mark the first element of the stack as not having any remaining siblings
                            deduplicated_children[0].second.set_last();
                        }

                        stack.insert(stack.end(), deduplicated_children.begin(), deduplicated_children.end());
                    }
                } else if constexpr (std::is_same_v<T, DisplayOp::Candidate>) {
                    auto candidate = std::any_cast<DisplayOp::Candidate>(arg);
                    auto node_index = candidate.node_index;
                    auto payload = graph.graph.get_node(node_index).get_payload();
                    auto solvable_id = std::get<ProblemNode::Solvable>(payload).solvable;

                    if (reported.find(solvable_id) != reported.end()) {
                        return;  // continue
                    }

                    auto solvable = pool->resolve_solvable(solvable_id);
                    auto name = pool->resolve_package_name(solvable.get_name_id());

                    //let version = if let Some(merged) = self.merged_candidates.get(&solvable_id) {
                    //                        reported.extend(merged.ids.iter().cloned());
                    //                        self.merged_solvable_display
                    //                            .display_candidates(&self.pool, &merged.ids)
                    //                    } else {
                    //                        self.merged_solvable_display
                    //                            .display_candidates(&self.pool, &[solvable_id])
                    //                    };
                    // in c++ it should be:

                    auto merged = merged_candidates.find(solvable_id);

                    std::string version;
                    if (merged != merged_candidates.end()) {
                        for (auto &id : merged->second.ids) {
                            reported.insert(id);
                        }
                        auto display_merged_solvable = DisplayMergedSolvable<VS, N>(pool, merged->second.ids);
                        version = display_merged_solvable.to_string();
                    } else {
                        auto display_merged_solvable = DisplayMergedSolvable<VS, N>(pool, merged->second.ids);
                        version = display_merged_solvable.to_string();
                    }

                    std::optional<ConflictCauseVariant> excluded;
                    for (auto &edge : graph.graph.outgoing_edges(node_index)) {
                        if (std::holds_alternative<ProblemEdge::Conflict>(edge.get_weight())) {
                            auto conflict = std::get<ProblemEdge::Conflict>(edge.get_weight());
                            if (std::holds_alternative<ConflictCause::Excluded>(conflict.cause)) {
                                auto excluded_reason = std::get<ConflictCause::Excluded>(conflict.cause);
                                auto target = edge.get_node_to();
                                auto target_payload = graph.graph.get_node(target).get_payload();
                                auto reason = std::get<ProblemNode::Excluded>(target_payload).reason;
                                excluded = reason;
                            } else {
                                excluded = std::nullopt;
                            }
                        }
                    }

                    auto already_installed = std::any_of(graph.graph.outgoing_edges(node_index).begin(), graph.graph.outgoing_edges(node_index).end(), [](auto &edge) {
                        return std::holds_alternative<ProblemEdge::Conflict>(edge.get_weight()) && std::holds_alternative<ConflictCause::ForbidMultipleInstances>(std::get<ProblemEdge::Conflict>(edge.get_weight()).cause);
                    });

                    auto constrains_conflict = std::any_of(graph.graph.outgoing_edges(node_index).begin(), graph.graph.outgoing_edges(node_index).end(), [](auto &edge) {
                        return std::holds_alternative<ProblemEdge::Conflict>(edge.get_weight()) && std::holds_alternative<ConflictCause::Constrains>(std::get<ProblemEdge::Conflict>(edge.get_weight()).cause);
                    });

                    auto is_leaf = graph.graph.outgoing_edges(node_index).begin() == graph.graph.outgoing_edges(node_index).end();

                    if (excluded.has_value()) {
                        oss << indent << name << " " << version << " is excluded because " << pool->resolve_string(excluded.value()) << std::endl;
                    } else if (is_leaf) {
                        oss << indent << name << " " << version << std::endl;
                    } else if (already_installed) {
                        oss << indent << name << " " << version << ", which conflicts with the versions reported above." << std::endl;
                    } else if (constrains_conflict) {
                        auto version_sets = std::copy_if(graph.graph.outgoing_edges(node_index).begin(), graph.graph.outgoing_edges(node_index).end(), [](auto &edge) {
                            return std::holds_alternative<ProblemEdge::Conflict>(edge.get_weight()) && std::holds_alternative<ConflictCause::Constrains>(std::get<ProblemEdge::Conflict>(edge.get_weight()).cause);
                        });

                        oss << indent << name << " " << version << " would constrain" << std::endl;
                        auto temp_indenter = node_indenter_pair.second.push_level();
                        for (auto &edge : version_sets) {
                            auto version_set_id = std::get<ProblemEdge::Conflict>(edge.get_weight()).cause;
                            auto version_set = pool->resolve_version_set(version_set_id);
                            auto version_set_package_name = pool->resolve_version_set_package_name(version_set_id);
                            version_set_package_name = pool->resolve_package_name(version_set_package_name);

                            if (version_sets.peek().is_none()) {
                                temp_indenter.set_last();
                            }
                            auto temp_indent = temp_indenter.get_indent();
                            oss << temp_indent << name << " " << version_set << ", which conflicts with any installable versions previously reported" << std::endl;
                        }
                    } else {
                        oss << indent << name << " " << version << " would require" << std::endl;

                        auto chunked_edges = std::copy_if(graph.graph.outgoing_edges(node_index).begin(), graph.graph.outgoing_edges(node_index).end(), [](auto &edge) {
                            return std::holds_alternative<ProblemEdge::Requires>(edge.get_weight());
                        });

                        std::unordered_map<VersionSetId, std::vector<Edge<ProblemNodeVariant, ProblemEdgeVariant>>> chunked;
                        for (auto &edge : chunked_edges) {
                            auto requires = std::get<ProblemEdge::Requires>(edge.get_weight());
                            chunked[requires.version_set_id].push_back(edge);
                        }

                        std::sort(chunked.begin(), chunked.end(), [this](auto &a, auto &b) {
                            auto a_edges = a.second;
                            auto b_edges = b.second;
                            auto a_installable = std::any_of(a_edges.begin(), a_edges.end(), [this](auto &edge) {
                                return installable_set.find(edge.get_node_to().get_id()) != installable_set.end();
                            });
                            auto b_installable = std::any_of(b_edges.begin(), b_edges.end(), [this](auto &edge) {
                                return installable_set.find(edge.get_node_to().get_id()) != installable_set.end();
                            });
                            return a_installable > b_installable;
                        });

                        std::vector<std::pair<DisplayOpVariant, Indenter>> requirements = std::map(chunked.begin(), chunked.end(), [&node_indenter_pair](auto &pair) {
                            // pair.first is version_set_id
                            // pair.second is vector of edges
                            return std::make_pair(DisplayOp::Requirement{pair.first, pair.second}, node_indenter_pair.second.push_level());
                        });

                        if (!requirements.empty()) {
                            requirements[0].second.set_last();
                        }

                        stack.insert(stack.end(), requirements.begin(), requirements.end());
                    }
                }
            }, node_indenter_pair.first);
        }

        return oss.str();
    }

};

#endif // DISPLAY_UNSAT_H