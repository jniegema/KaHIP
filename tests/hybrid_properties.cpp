#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include "check.h"
#include "configuration.h"
#include "node_ordering/ordering_config.h"
#include "partition/uncoarsening/separator/area_bfs.h"
#include "tools/graph_extractor.h"
#include "tools/task_pool.h"

int main() {
        try {
                typedef void (configuration::*preset)(PartitionConfig &);
                const preset presets[] = {&configuration::standard, &configuration::fast_separator,
                                          &configuration::eco_separator, &configuration::strong_separator};
                configuration settings;
                for (preset initialize : presets) {
                        PartitionConfig config;
                        config.k = 2;
                        config.threads = 73;
                        config.metis_depth = 23;
                        (settings.*initialize)(config);
                        std::cout << "preset defaults: threads=" << config.threads << ", metis_depth=" << config.metis_depth << '\n';
                        require(config.threads == 0 && config.metis_depth == -1, "presets must initialize library ordering defaults");
                        validate_node_ordering_config(config);
                }
                require(!exceeds_ordering_degree(1520, 400, 1073741824u), "density comparison must not overflow in 32-bit builds");
                require(!exceeds_ordering_degree(1500, 10, 150), "density threshold must remain strict");
                require(exceeds_ordering_degree(1501, 10, 150), "density threshold must detect a denser graph");
                require(!exceeds_ordering_degree(1501, 10, 0), "zero must disable the density threshold");

                graph_access graph;
                graph.start_construction(8, 16);
                graph.set_partition_count(2);
                for (NodeID v = 0; v < 8; ++v) {
                        graph.new_node();
                        graph.setNodeWeight(v, v + 1);
                        graph.setPartitionIndex(v, v / 4);
                        for (NodeID target : {(v + 1) % 8, (v + 7) % 8}) {
                                const EdgeID edge = graph.new_edge(v, target);
                                graph.setEdgeWeight(edge, v + target + 1);
                        }
                }
                graph.finish_construction();
                graph_access copy;
                graph.copy(copy);
                copy.setPartitionIndex(0, 1);
                copy.setNodeWeight(0, 99);
                copy.set_contraction_offset(0, 17);
                copy.setEdgeWeight(0, 33);
                copy.setEdgeRating(0, 71);
                graph.copy(copy);
                for (NodeID v = 0; v < 8; ++v) {
                        require(copy.getNodeWeight(v) == graph.getNodeWeight(v), "reused copies must restore node weights");
                        require(copy.getPartitionIndex(v) == 0 && copy.get_contraction_offset(v) == 0,
                                "reused copies must reset partition and reduction state");
                }
                for (EdgeID e = 0; e < graph.number_of_edges(); ++e) {
                        require(copy.getEdgeTarget(e) == graph.getEdgeTarget(e) && copy.getEdgeWeight(e) == graph.getEdgeWeight(e),
                                "reused copies must preserve edge order and weights");
                        require(copy.getEdgeRating(e) == 0, "reused copies must reset coarsening state");
                }
                for (PartitionID block = 0; block < 2; ++block) {
                        graph_access child;
                        std::vector<NodeID> mapping;
                        graph_extractor().extract_block(graph, child, block, mapping);
                        require(child.number_of_nodes() == 4 && child.number_of_edges() == 6, "child must contain exactly its induced subgraph");
                        for (NodeID v = 0; v < 4; ++v) {
                                require(mapping[v] == block * 4 + v, "extraction must retain vertex order");
                                std::vector<std::pair<NodeID, EdgeWeight>> expected, actual;
                                forall_out_edges(graph, e, mapping[v]) {
                                        if (graph.getPartitionIndex(graph.getEdgeTarget(e)) == block)
                                                expected.emplace_back(graph.getEdgeTarget(e), graph.getEdgeWeight(e));
                                } endfor
                                forall_out_edges(child, e, v) {
                                        actual.emplace_back(mapping[child.getEdgeTarget(e)], child.getEdgeWeight(e));
                                } endfor
                                require(actual == expected, "extraction must retain adjacency order and edge weights");
                        }
                }

                for (bool threaded : {false, true}) {
                        random_functions::scoped_thread_streams mode(threaded);
                        random_functions::setSeed(42);
                        const unsigned expected_int = random_functions::nextInt(0, 1000000);
                        const double expected_double = random_functions::nextDouble(0, 1);
                        random_functions::setSeed(42);
                        area_bfs::m_deepth.assign(64, 73);
                        area_bfs::round = 91;
                        {
                                area_bfs::scoped_task state;
                                random_functions::setSeed(99);
                                area_bfs::m_deepth.assign(2, 1);
                                area_bfs::round = 2;
                        }
                        require(random_functions::nextInt(0, 1000000) == expected_int, "helping a task must preserve the caller's integer stream");
                        require(random_functions::nextDouble(0, 1) == expected_double, "helping a task must preserve the caller's double stream");
                        require(area_bfs::m_deepth == std::vector<int>(64, 73) && area_bfs::round == 91,
                                "helping a task must preserve the caller's BFS marks");
                }

                for (int threads : {1, 2, 6}) {
                        task_pool pool(threads);
                        std::atomic<int> completed(0);
                        std::mutex mutex;
                        std::set<std::thread::id> workers;
                        task_pool::group outer;
                        for (int i = 0; i < 100; ++i) {
                                outer.run([&] {
                                        task_pool::group inner;
                                        for (int j = 0; j < 4; ++j) inner.run([&] {
                                                require(task_pool::concurrency() == threads, "nested tasks must retain the pool");
                                                std::lock_guard<std::mutex> lock(mutex);
                                                workers.insert(std::this_thread::get_id());
                                                ++completed;
                                        });
                                        inner.wait();
                                });
                        }
                        outer.wait();
                        require(completed == 400 && workers.size() <= static_cast<size_t>(threads),
                                "nested tasks must finish within the requested worker count");
                        bool propagated = false;
                        try {
                                task_pool::group failures;
                                failures.run([] { throw std::runtime_error("intentional worker failure"); });
                                failures.run([&] { ++completed; });
                                failures.wait();
                        } catch (const std::runtime_error &error) {
                                propagated = std::string(error.what()) == "intentional worker failure";
                        }
                        require(propagated && completed == 401, "worker failures must propagate after all tasks finish");
                        try {
                                task_pool::group unwinding;
                                unwinding.run([&] { ++completed; });
                                throw std::runtime_error("caller failure");
                        } catch (const std::runtime_error &) {}
                        require(completed == 402, "unwinding must drain tasks before their captures are destroyed");
                        std::cout << "threads=" << threads << ", completed=" << completed << ", workers=" << workers.size() << '\n';
                }
        } catch (const std::exception &error) {
                std::cerr << error.what() << '\n';
                return 1;
        }
}
