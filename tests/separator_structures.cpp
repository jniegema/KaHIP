#include <algorithm>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <vector>
#include "check.h"
#include "data_structure/priority_queues/maxNodeHeap.h"
#include "algorithms/push_relabel.h"
#include "partition/uncoarsening/separator/vertex_separator_algorithm.h"

int main() {
        try {
                std::mt19937 rng(7654321);
                maxNodeHeap heap;
                maxNodeHeap base(2000);
                std::map<unsigned, int> reference;
                for (int i = 0; i < 100000; ++i) {
                        const unsigned id = rng() % 2000;
                        const int gain = static_cast<int>(rng() % 100) - 50;
                        if (reference.empty() || rng() % 3 == 0) {
                                if (!reference.count(id)) reference[id] = gain;
                                heap.insert(id, gain);
                                base.insert(id, gain);
                        } else if (rng() % 2 == 0) {
                                auto item = reference.begin();
                                std::advance(item, rng() % reference.size());
                                item->second = gain;
                                heap.changeKey(item->first, gain);
                                base.changeKey(item->first, gain);
                        } else {
                                require(heap.maxValue() == base.maxValue() && heap.maxElement() == base.maxElement(),
                                        "sparse and dense heaps disagree on the maximum");
                                const int expected = std::max_element(reference.begin(), reference.end(),
                                        [](const std::pair<const unsigned, int>& a, const std::pair<const unsigned, int>& b) { return a.second < b.second; })->second;
                                require(heap.maxValue() == expected, "heap maximum differs from the independent map");
                                const unsigned deleted = heap.deleteMax();
                                require(deleted == base.deleteMax(), "sparse and dense heaps disagree on ties");
                                reference.erase(deleted);
                        }
                        require(heap.size() == reference.size() && heap.size() == base.size(),
                                "heap size differs from the independent map");
                }
                std::cout << "heap_operations=100000 failures=0\n";
                for (int trial = 0; trial < 500; ++trial) {
                        const int n = 2 + rng() % 12;
                        flow_graph graph;
                        graph.start_construction(n);
                        std::vector<std::vector<long>> residual(n, std::vector<long>(n));
                        std::vector<std::vector<std::pair<unsigned, long>>> edges(n);
                        for (int i = 0; i < n * n; ++i) {
                                const int u = rng() % n, v = rng() % n;
                                if (u == v) continue;
                                const long cap = 1 + rng() % 30;
                                graph.new_edge(u, v, cap);
                                residual[u][v] += cap;
                                edges[u].emplace_back(v, cap);
                                edges[v].emplace_back(u, 0);
                        }
                        graph.finish_construction();
                        graph.finish_construction();
                        for (int u = 0; u < n; ++u) {
                                require(graph.get_first_invalid_edge(u) == edges[u].size(), "residual adjacency has the wrong size");
                                for (unsigned e = 0; e < edges[u].size(); ++e) {
                                        const unsigned v = graph.getEdgeTarget(u, e);
                                        const unsigned rev = graph.getReverseEdge(u, e);
                                        require(v == edges[u][e].first && graph.getEdgeCapacity(u, e) == edges[u][e].second,
                                                "residual edges lost insertion order or capacity");
                                        require(graph.getEdgeTarget(v, rev) == u && graph.getReverseEdge(v, rev) == e,
                                                "reverse edge is not an involution");
                                }
                        }
                        long expected = 0;
                        for (;;) {
                                std::vector<int> parent(n, -1); parent[0] = 0;
                                std::queue<int> queue; queue.push(0);
                                while (!queue.empty()) {
                                        const int u = queue.front(); queue.pop();
                                        for (int v = 0; v < n; ++v) if (parent[v] < 0 && residual[u][v] > 0) {
                                                parent[v] = u; queue.push(v);
                                        }
                                }
                                if (parent[n - 1] < 0) break;
                                long cap = 1000000;
                                for (int v = n - 1; v != 0; v = parent[v]) cap = std::min(cap, residual[parent[v]][v]);
                                for (int v = n - 1; v != 0; v = parent[v]) {
                                        residual[parent[v]][v] -= cap; residual[v][parent[v]] += cap;
                                }
                                expected += cap;
                        }
                        push_relabel solver;
                        std::vector<NodeID> cut;
                        const auto actual = solver.solve_max_flow_min_cut(graph, 0, n - 1, true, cut);
                        if (actual != expected) {
                                std::cout << "flow trial=" << trial << " actual=" << actual << " expected=" << expected << '\n';
                                throw std::runtime_error("push-relabel differs from Edmonds-Karp");
                        }
                }
                std::cout << "flow_graphs=500 failures=0\n";

                maxNodeHeap sparse, dense(32);
                for (maxNodeHeap *queue : {&sparse, &dense}) {
                                queue->insert(4, 0);
                                queue->insert(9, 3);
                                queue->changeKey(17, 5);
                                require(queue->maxElement() == 4 && queue->maxValue() == 5,
                                                "absent-node key updates must preserve the legacy alias");
                                require(!queue->owns_key(4) && !queue->owns_key(17) && queue->owns_key(9),
                                                "only an independent nonzero element owns its key");
                }
                maxNodeHeap local;
                local.insert(10000000, 7);
                local.insert(10000001, 9);
                require(local.deleteMax() == 10000001 && local.deleteMax() == 10000000,
                                "localized queues must support large global vertex IDs");

                graph_access path;
                path.start_construction(8, 14);
                for (NodeID v = 0; v < 8; ++v) {
                                path.new_node();
                                path.setNodeWeight(v, v + 1);
                                if (v > 0) path.setEdgeWeight(path.new_edge(v, v - 1), 1);
                                if (v < 7) path.setEdgeWeight(path.new_edge(v, v + 1), 1);
                }
                path.finish_construction();
                PartitionConfig config;
                vertex_separator_algorithm separator;
                for (NodeID center : {2u, 4u, 3u}) {
                                for (NodeID v = 0; v < 8; ++v) path.setPartitionIndex(v, v < center ? 0 : v == center ? 2 : 1);
                                std::vector<NodeID> left{center - 1}, middle{center}, right{center + 1}, mapping;
                                flow_graph flow;
                                NodeID source, sink;
                                separator.build_flow_problem(config, path, left, right, middle, flow, mapping, source, sink);
                                std::vector<NodeID> cut;
                                const FlowType value = push_relabel().solve_max_flow_min_cut(flow, source, sink, true, cut);
                                std::cout << "reused flow mapping: cut=" << value << ", expected=" << center << '\n';
                                require(value == center, "a reused mapping must cut the lightest vertex of the current path region");
                }
        } catch (const std::exception &error) {
                std::cerr << error.what() << '\n';
                return 1;
        }
}
