#ifndef PROBLEM_H
#define PROBLEM_H

#include <iostream>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <variant>
#include <memory>
#include <algorithm>
#include <deque>
#include "internal/ClauseId.h"
#include "internal/SolvableId.h"

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
    uint32_t id;
    P payload;
public:
    explicit Node(P payload) : payload(payload) {
        id = node_count++;
    }
    P get_payload() {
        return payload;
    }
    uint32_t get_id() const {
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
    void add_node(Node<P> node) {
        nodes.push_back(node);
    }
    void add_edge(Node<P> node_from, Node<P> node_to, W weight) {
        edges.push_back({node_from, node_to, weight});
    }
    uint32_t node_count() {
        return nodes.size();
    }
    std::vector<Edge<Node<P>, W>> incoming_edges(Node<P> node) {
        std::vector<Edge<Node<P>, W>> incoming;
        for (auto& edge : edges) {
            if (edge.node_to == node) {
                incoming.push_back(edge);
            }
        }
        return incoming;
    }
    std::vector<Edge<Node<P>, W>> outgoing_edges(Node<P> node) {
        std::vector<Edge<Node<P>, W>> outgoing;
        for (auto& edge : edges) {
            if (edge.node_from == node) {
                outgoing.push_back(edge);
            }
        }
        return outgoing;
    }
};

template<typename N, typename E>
class BFS {
private:
    DiGraph<N, E> graph;
    uint32_t root_node;
    std::deque<uint32_t> queue;
public:
    BFS(DiGraph<N, E> graph, uint32_t root_node) : graph(graph), root_node(root_node) {
        queue.push_back(root_node);
    }

    std::optional<N> next() {
        if (queue.empty()) {
            return std::nullopt;
        }
        auto node = queue.front();
        queue.pop_front();
        for (auto& edge : graph.edges) {
            if (std::get<0>(edge) == node) {
                queue.push_back(std::get<1>(edge));
            }
        }
        return graph.nodes[node];
    }
};

typedef uint32_t NodeIndex;

template<typename P, typename E>
class DfsPostOrder {
private:
    DiGraph<P, E> graph;
    Node<P> root_node;
    std::unordered_set<Node<P>> visited;
public:
    DfsPostOrder(DiGraph<P, E> graph, Node<P> root_node) : graph(graph), root_node(root_node) {
    }

    // next node in post-order traversal
    std::optional<Node<P>> next() {
        if (visited.size() == graph.node_count()) {
            return std::nullopt;
        }
        for (auto& node : graph.nodes) {
            if (visited.find(node) == visited.end()) {
                visited.insert(node);
                return node;
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
struct ProblemGraph {
    DiGraph<ProblemNodeVariant, ProblemEdgeVariant> graph;
    Node<ProblemNodeVariant> root_node;
    std::optional<Node<ProblemNodeVariant>> unresolved_node;

    std::unordered_set<Node<ProblemNodeVariant>> get_installable_set() {
        std::unordered_set<Node<ProblemNodeVariant>> installable;
        DfsPostOrder<ProblemNodeVariant, ProblemEdgeVariant> dfs(graph, root_node);
        while (auto node = dfs.next()) {
            if (unresolved_node == node) {
                continue;
            }
            bool excluding_edges = false;
            for (auto& edge : graph.incoming_edges(node.value())) {
                if (edge.node_to == node) {
                    if (std::holds_alternative<ProblemEdge::Conflict>(edge.weight)) {
                        excluding_edges = true;
                        break;
                    }
                }
            }
            if (excluding_edges) {
                continue;
            }
            bool outgoing_conflicts = false;
            for (auto& edge : graph.outgoing_edges(node.value())) {
                if (edge.node_from == node) {
                    if (std::holds_alternative<ProblemEdge::Conflict>(edge.weight)) {
                        outgoing_conflicts = true;
                        break;
                    }
                }
            }
            if (outgoing_conflicts) {
                continue;
            }
            std::unordered_map<VersionSetId, Node<ProblemNodeVariant>> dependencies;
            for (auto& edge : graph.outgoing_edges(node.value())) {
                if (edge.node_from == node) {
                    if (std::holds_alternative<ProblemEdge::Requires>(edge.weight)) {
                        // dependencies[edge.weight] = edge.node_to;
                        dependencies.insert(std::pair(std::get<ProblemEdge::Requires>(edge.weight).version, edge.node_to));
                    }
                }
            }
            bool all_deps_installable = true;
            for (auto& [version_set_id, target] : dependencies) {
                if (installable.find(target) == installable.end()) {
                    all_deps_installable = false;
                    break;
                }
            }
            if (!all_deps_installable) {
                continue;
            }
            installable.insert(node.value());
        }
        return installable;
    }

    std::unordered_set<Node<ProblemNodeVariant>> get_missing_set() {
        std::unordered_set<Node<ProblemNodeVariant>> missing;
        if (!unresolved_node.has_value()) {
            return missing;
        }
        missing.insert(unresolved_node.value());
        DfsPostOrder<ProblemNodeVariant, ProblemEdgeVariant> dfs(graph, root_node);
        while (auto node = dfs.next()) {
            bool outgoing_conflicts = false;
            for (auto& edge : graph.outgoing_edges(node.value())) {
                if (edge.node_from == node) {
                    if (std::holds_alternative<ProblemEdge::Conflict>(edge.weight)) {
                        outgoing_conflicts = true;
                        break;
                    }
                }
            }
            if (outgoing_conflicts) {
                continue;
            }
            std::unordered_map<VersionSetId, Node<ProblemNodeVariant>> dependencies;
            for (auto& edge : graph.outgoing_edges(node.value())) {
                if (edge.node_from == node) {
                    if (std::holds_alternative<ProblemEdge::Requires>(edge.weight)) {
                        dependencies.insert(std::pair(std::get<ProblemEdge::Requires>(edge.weight).version, edge.node_to));
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
                missing.insert(node.value());
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


};


#endif // PROBLEM_H