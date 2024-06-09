#ifndef PROBLEM_H
#define PROBLEM_H

#include <iostream>
#include <utility>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <variant>
#include <memory>
#include <algorithm>
#include <deque>
#include "internal/ClauseId.h"
#include "internal/SolvableId.h"

typedef uint32_t NodeIndex;

namespace ProblemNode {
    struct Solvable {
        SolvableId solvable;
    };
    struct UnresolvedDependency {
    };
    struct Excluded {
        StringId excluded;
    };
}

using ProblemNodeVariant = std::variant<ProblemNode::Solvable, ProblemNode::UnresolvedDependency, ProblemNode::Excluded>;


namespace ConflictCause {
    // The solvable is locked
    struct Locked {
        SolvableId solvable;
    };
    // The target node is constrained by the specified version set
    struct Constrains {
        VersionSetId version;
    };
    // It is forbidden to install multiple instances of the same dependency
    struct ForbidMultipleInstances {
    };
    // The node was excluded
    struct Excluded {
    };
}
using ConflictCauseVariant = std::variant<ConflictCause::Locked, ConflictCause::Constrains, ConflictCause::ForbidMultipleInstances, ConflictCause::Excluded>;

namespace ProblemEdge {
    // The target node is a candidate for the dependency specified by the version set
    struct Requires {
        VersionSetId version;
    };
    // The target node is involved in a conflict, caused by `conflict_cause`
    struct Conflict {
        ConflictCauseVariant conflict_cause;
    };
}

using ProblemEdgeVariant = std::variant<ProblemEdge::Requires, ProblemEdge::Conflict>;

// Represents a node that has been merged with others
//
// Merging is done to simplify error messages, and happens when a group of nodes satisfies the
// following criteria:
//
// - They all have the same name
// - They all have the same predecessor nodes
// - They all have the same successor nodes
struct MergedProblemNode {
    std::vector<SolvableId> ids;
};

template<typename N, typename W>
class Edge {
public:
    N node_from;
    N node_to;
    W weight;
};

static uint32_t node_count = 0;

template<typename P>
class Node {
private:
    NodeIndex id;
    P payload;
public:
    explicit Node(P payload) : payload(payload), id(node_count++) {}
    P get_payload() {
        return payload;
    }
    NodeIndex get_id() const {
        return id;
    }
    bool operator==(const Node& other) const {
        return id == other.id;
    }
};

namespace std {
    template<typename P>
    struct hash<Node<P>> {
    std::size_t operator()(const Node<P>& node) const {
        return std::hash<uint32_t>{}(node.get_id());
    }
};
}

template<typename P, typename W>
class DiGraph {
public:
    std::vector<Node<P>> nodes;
    std::vector<Edge<Node<P>, W>> edges;
    NodeIndex add_node(P payload) {
        Node<P> node(payload);
        nodes.push_back(node);
        return node.get_id();
    }
    void add_edge(Node<P> node_from, Node<P> node_to, W weight) {
        edges.push_back({node_from, node_to, weight});
    }
    uint32_t node_count() {
        return nodes.size();
    }
    std::vector<Edge<Node<P>, W>> incoming_edges(NodeIndex nodeIndex) {
        std::vector<Edge<Node<P>, W>> incoming;
        for (auto& edge : edges) {
            if (edge.node_to.get_id() == nodeIndex) {
                incoming.push_back(edge);
            }
        }
        return incoming;
    }
    std::vector<Edge<Node<P>, W>> outgoing_edges(NodeIndex nodeIndex) {
        std::vector<Edge<Node<P>, W>> outgoing;
        for (auto& edge : edges) {
            if (edge.node_from.get_id() == nodeIndex) {
                outgoing.push_back(edge);
            }
        }
        return outgoing;
    }

    void remove_node(NodeIndex node_index) {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [node_index](Node<P> node) {
            return node.get_id() == node_index;
        }), nodes.end());
        edges.erase(std::remove_if(edges.begin(), edges.end(), [node_index](Edge<Node<P>, W> edge) {
            return edge.node_from.get_id() == node_index || edge.node_to.get_id() == node_index;
        }), edges.end());
    }
};

template<typename N, typename E>
class Bfs {
private:
    DiGraph<N, E> graph;
    NodeIndex root_node;
    std::deque<NodeIndex> queue;
public:
    Bfs(DiGraph<N, E> graph, NodeIndex root_node) : graph(graph), root_node(root_node) {
        queue.push_back(root_node);
    }

    std::optional<NodeIndex> next() {
        if (queue.empty()) {
            return std::nullopt;
        }
        auto node_index = queue.front();
        queue.pop_front();
        for (auto& edge : graph.edges) {
            if (edge.node_from == node_index) {
                queue.push_back(edge.node_to.get_id());
            }
        }
        return node_index;
    }
};

template<typename P, typename E>
class DfsPostOrder {
private:
    DiGraph<P, E> graph;
    NodeIndex root_node;
    std::unordered_set<Node<P>> visited;
public:
    DfsPostOrder(DiGraph<P, E> graph, NodeIndex root_node) : graph(std::move(graph)), root_node(root_node) {}

    // next node in post-order traversal
    std::optional<NodeIndex> next() {
        if (visited.size() == graph.node_count()) {
            return std::nullopt;
        }
        for (auto& node : graph.nodes) {
            if (visited.find(node) == visited.end()) {
                visited.insert(node);
                return node.get_id();
            }
        }
        return std::nullopt;
    }
};

// Graph representation of [`Problem`]
//
// The root of the graph is the "root solvable". Note that not all the solvable's requirements are
// included in the graph, only those that are directly or indirectly involved in the conflict. See
// [`ProblemNode`] and [`ProblemEdge`] for the kinds of nodes and edges that make up the graph.
class ProblemGraph {
private:
    DiGraph<ProblemNodeVariant, ProblemEdgeVariant> graph;
    NodeIndex root_node;
    std::optional<NodeIndex> unresolved_node;
public:
    ProblemGraph(DiGraph<ProblemNodeVariant, ProblemEdgeVariant> graph, NodeIndex root_node, std::optional<NodeIndex> unresolved_node) : graph(std::move(graph)), root_node(root_node), unresolved_node(unresolved_node) {}

    std::unordered_set<NodeIndex> get_installable_set() {
        std::unordered_set<NodeIndex> installable;
        DfsPostOrder<ProblemNodeVariant, ProblemEdgeVariant> dfs(graph, root_node);
        while (auto optional_node_index = dfs.next()) {
            if (unresolved_node == optional_node_index) {
                // The unresolved node isn't installable
                continue;
            }
            auto node_index = optional_node_index.value();
            bool excluding_edges = false;
            for (auto& edge : graph.incoming_edges(node_index)) {
                if (std::holds_alternative<ProblemEdge::Conflict>(edge.weight)) {
                    // check if ConflictCause::Excluded
                    auto conflict_cause = std::get<ProblemEdge::Conflict>(edge.weight).conflict_cause;
                    if (std::holds_alternative<ConflictCause::Excluded>(conflict_cause)) {
                        excluding_edges = true;
                        break;
                    }
                }
            }
            if (excluding_edges) {
                // Nodes with incoming disabling edges aren't installable
                continue;
            }
            bool outgoing_conflicts = false;
            for (auto& edge : graph.outgoing_edges(node_index)) {
                if (std::holds_alternative<ProblemEdge::Conflict>(edge.weight)) {
                    outgoing_conflicts = true;
                    break;
                }
            }
            if (outgoing_conflicts) {
                continue;
            }
            std::unordered_map<VersionSetId, NodeIndex> dependencies;
            for (auto& edge : graph.outgoing_edges(node_index)) {
                if (std::holds_alternative<ProblemEdge::Requires>(edge.weight)) {
                    // dependencies[edge.weight] = edge.node_to;
                    dependencies.insert(std::pair(std::get<ProblemEdge::Requires>(edge.weight).version, edge.node_to.get_id()));
                }
            }
            bool all_deps_installable = true;
            for (const auto& [version_set_id, target] : dependencies) {
                if (installable.find(target) == installable.end()) {
                    all_deps_installable = false;
                    break;
                }
            }
            if (!all_deps_installable) {
                continue;
            }
            installable.insert(node_index);
        }
        return installable;
    }

    std::unordered_set<NodeIndex> get_missing_set() {
        std::unordered_set<NodeIndex> missing;
        if (!unresolved_node.has_value()) {
            return missing;
        }
        missing.insert(unresolved_node.value());
        DfsPostOrder<ProblemNodeVariant, ProblemEdgeVariant> dfs(graph, root_node);
        while (auto optional_node_index = dfs.next()) {
            auto node_index = optional_node_index.value();
            bool outgoing_conflicts = false;
            for (auto& edge : graph.outgoing_edges(node_index)) {
                if (edge.node_from.get_id() == optional_node_index) {
                    if (std::holds_alternative<ProblemEdge::Conflict>(edge.weight)) {
                        outgoing_conflicts = true;
                        break;
                    }
                }
            }
            if (outgoing_conflicts) {
                continue;
            }
            std::unordered_map<VersionSetId, NodeIndex> dependencies;
            for (auto& edge : graph.outgoing_edges(node_index)) {
                if (edge.node_from.get_id() == optional_node_index) {
                    if (std::holds_alternative<ProblemEdge::Requires>(edge.weight)) {
                        dependencies.insert(std::pair(std::get<ProblemEdge::Requires>(edge.weight).version, edge.node_to.get_id()));
                    }
                }
            }
            bool all_deps_missing = true;
            for (auto& [version_set_id, target] : dependencies) {
                if (missing.find(target) == missing.end()) {
                    all_deps_missing = false;
                    break;
                }
            }
            if (all_deps_missing) {
                missing.insert(optional_node_index.value());
            }
        }
        return missing;
    }
};


enum ChildOrder {
    HasRemainingSiblings,
    Last,
};

class Indenter {
private:
    std::vector<ChildOrder> levels;
    bool top_level_indent;
public:
    Indenter(bool top_level_indent) : top_level_indent(top_level_indent) {
    }

    bool is_at_top_level() {
        return levels.size() == 1;
    }

    void push_level() {
        push_level_with_order(ChildOrder::HasRemainingSiblings);
    }

    void push_level_with_order(ChildOrder order) {
        levels.push_back(order);
    }

    void set_last() {
        levels.back() = ChildOrder::Last;
    }

    std::string get_indent() {
        assert(!levels.empty());

        std::string s;

        auto deepest_level = levels.size() - 1;

        for (auto level = 0; level < levels.size(); level++) {
            if (level == 0 && !top_level_indent) {
                continue;
            }

            auto is_at_deepest_level = level == deepest_level;

            std::string tree_prefix;
            if (is_at_deepest_level) {
                if (levels[level] == ChildOrder::HasRemainingSiblings) {
                    tree_prefix = "├─";
                } else {
                    tree_prefix = "└─";
                }
            } else {
                if (levels[level] == ChildOrder::HasRemainingSiblings) {
                    tree_prefix = "│ ";
                } else {
                    tree_prefix = "  ";
                }
            }

            s += tree_prefix;
            s += ' ';
        }

        return s;
    }
};

class Problem {
public:
    std::vector<ClauseId> clauses;
    Problem() = default;

    void add_clause(const ClauseId& clauseId) {
        if (std::find(clauses.cbegin(), clauses.cend(), clauseId) == clauses.cend()) {
            clauses.push_back(clauseId);
        }
    }

    NodeIndex add_node(DiGraph<ProblemNodeVariant, ProblemEdgeVariant>& graph, std::unordered_map<SolvableId, NodeIndex>& nodes, SolvableId solvable_id) {
        if (nodes.find(solvable_id) == nodes.end()) {
            auto node_index = graph.add_node(ProblemNode::Solvable{solvable_id});
            nodes.insert(std::pair(solvable_id, node_index));
        }
        return nodes.at(solvable_id);
    }
};


#endif // PROBLEM_H