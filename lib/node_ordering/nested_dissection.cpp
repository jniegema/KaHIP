/*
 * Author: Wolfgang Ost
 */

#include <algorithm>
#include <iostream>
#include <utility>
#include <vector>

#include "balance_configuration.h"
#include "node_ordering/min_degree_ordering.h"
#include "node_ordering/nested_dissection.h"
#include "node_ordering/reductions.h"
#include "partition/graph_partitioner.h"
#include "partition/uncoarsening/separator/area_bfs.h"
#include "partition/uncoarsening/separator/vertex_separator_algorithm.h"
#include "tools/graph_extractor.h"
#include "tools/macros_assertions.h"
#include "tools/quality_metrics.h"
#include "tools/random_functions.h"
#include "tools/thread_budget.h"

#include <cstdlib>
#include <limits>
#include <mutex>
#include <thread>

#ifdef USEMETIS
#include "metis.h"
#endif

namespace {
// METIS draws from GKlib's generator, which is the C library's rand() unless
// GKlib was built with GKRAND (then a Mersenne twister in static variables):
// process-wide state either way with glibc, and every METIS call reseeds it on
// entry. Two calls on two threads would interleave their draws and the
// ordering would depend on scheduling, so METIS calls take turns: in sequence,
// each call's draws are its own whichever thread runs it, and when. (Under
// --threads, random_functions::setSeed leaves srand() to METIS for the same
// reason.)
#ifdef USEMETIS
std::mutex metis_mutex;
#endif

// The subgraph's elimination order by METIS_NodeND, into labels (label[v]:
// v's position). METIS's seed comes from KaHIP's generator, so a KaHIP seed
// fixes the whole ordering and different seeds give different ones.
void metis_ordering(graph_access &G, int nseps, std::vector<NodeID> &labels) {
#ifdef USEMETIS
        idx_t n = G.number_of_nodes();
        std::vector<idx_t> xadj(n + 1), adjncy(G.number_of_edges()), vwgt(n), perm(n), iperm(n);
        forall_nodes(G, node) {
                xadj[node] = G.get_first_edge(node);
                vwgt[node] = G.getNodeWeight(node);
        } endfor
        xadj[n] = G.number_of_edges();
        forall_edges(G, e) {
                adjncy[e] = G.getEdgeTarget(e);
        } endfor
        idx_t options[METIS_NOPTIONS];
        METIS_SetDefaultOptions(options);
        options[METIS_OPTION_NSEPS] = nseps;
        options[METIS_OPTION_SEED]  = random_functions::nextInt(0, 1 << 30);
        std::lock_guard<std::mutex> lock(metis_mutex);
        if (METIS_NodeND(&n, xadj.data(), adjncy.data(), vwgt.data(), options, perm.data(), iperm.data()) != METIS_OK) {
                std::cerr << "METIS_NodeND failed on a subgraph of " << n << " vertices" << std::endl;
                std::exit(1);
        }
        labels.assign(iperm.begin(), iperm.end());
#else
        (void)G; (void)nseps; (void)labels;
        std::cerr << "metis_below needs node_ordering built with METIS" << std::endl;
        std::exit(1);
#endif
}

// One METIS bisection of G into blocks 0 and 1 and separator block 2: the
// separator METIS_NodeND computes at the top of its own recursion (the same
// MlevelNodeBisectionMultiple, nseps candidates), left on G for
// dissect_children.
void metis_separator(graph_access &G, int nseps) {
#ifdef USEMETIS
        idx_t n = G.number_of_nodes();
        std::vector<idx_t> xadj(n + 1), adjncy(G.number_of_edges()), vwgt(n), part(n);
        forall_nodes(G, node) {
                xadj[node] = G.get_first_edge(node);
                vwgt[node] = G.getNodeWeight(node);
        } endfor
        xadj[n] = G.number_of_edges();
        forall_edges(G, e) {
                adjncy[e] = G.getEdgeTarget(e);
        } endfor
        idx_t options[METIS_NOPTIONS];
        METIS_SetDefaultOptions(options);
        options[METIS_OPTION_NSEPS] = nseps;
        options[METIS_OPTION_SEED]  = random_functions::nextInt(0, 1 << 30);
        idx_t separator_weight = 0;
        std::lock_guard<std::mutex> lock(metis_mutex);
        if (METIS_ComputeVertexSeparator(&n, xadj.data(), adjncy.data(), vwgt.data(), options, &separator_weight, part.data()) != METIS_OK) {
                std::cerr << "METIS_ComputeVertexSeparator failed on a subgraph of " << n << " vertices" << std::endl;
                std::exit(1);
        }
        G.set_partition_count(3);
        G.setSeparatorBlock(2);
        forall_nodes(G, node) {
                G.setPartitionIndex(node, part[node]);
        } endfor
#else
        (void)G; (void)nseps;
        std::cerr << "metis_split needs node_ordering built with METIS" << std::endl;
        std::exit(1);
#endif
}
}

nested_dissection::nested_dissection(graph_access * const G) :
        original_graph(G), m_recursion_level(0) {}

nested_dissection::nested_dissection(graph_access * const G, int recursion_level) :
        original_graph(G), m_recursion_level(recursion_level) {}


void nested_dissection::perform_nested_dissection(PartitionConfig &config) {
        if (m_recursion_level == 0 && config.threads > 0) {
                random_functions::use_thread_streams();
        }
        if (original_graph->number_of_nodes() == 0) {
                return;
        }

        // 'reduced_graph' is a copy of 'original_graph', with reductions applied.
        // If no reductions were applied, 'reduced_graph' is empty, and we use 'original_graph' instead.
        graph_access reduced_graph;
        graph_access *active_graph;
        bool used_reductions = apply_reductions(config, *original_graph, m_reduction_stack, m_recursion_level);
        if (used_reductions) {
                active_graph = &m_reduction_stack.back()->get_reduced_graph();
        } else {
                active_graph = original_graph;
        }

        m_reduced_label.resize(active_graph->number_of_nodes());

        if (active_graph->number_of_nodes() > 0) {
                if (active_graph->number_of_nodes() < config.dissection_rec_limit) {
                        // Stop nested dissection and use the min degree algorithm instead
                        MinDegree(active_graph).perform_ordering(m_reduced_label);
                } else if (active_graph->number_of_nodes() < config.metis_below
                           || (config.metis_depth >= 0 && m_recursion_level >= config.metis_depth)) {
                        if (config.metis_split > 0 && active_graph->number_of_nodes() >= config.metis_split) {
                                if (config.threads > 0 && m_recursion_level == 0) thread_budget::set(config.threads);
                                metis_separator(*active_graph, config.metis_nseps);
                                dissect_children(config, *active_graph);
                        } else {
                                metis_ordering(*active_graph, config.metis_nseps, m_reduced_label);
                        }
                } else if (config.threads > 0) {
                        if (m_recursion_level == 0) thread_budget::set(config.threads);
                        compute_separator(config, *active_graph);
                        dissect_children(config, *active_graph);
                } else {
                        NodeID order_begin = 0;
                        // continue nested dissection
                        compute_separator(config, *active_graph);

                        // perform nested dissection on subgraphs
                        forall_blocks((*active_graph), p) {
                                if (p != active_graph->getSeparatorBlock()) {
                                        recurse_dissection(config, (*active_graph), p, order_begin);
                                }
                        } endfor
                        // Perform nested dissection on separator block
                        recurse_dissection(config, (*active_graph), active_graph->getSeparatorBlock(), order_begin);
                }
        }

        if (used_reductions) {
                // Map the ordering from the reduced graph to the original graph
                map_ordering(m_reduction_stack, m_reduced_label, m_label);
        } else {
                m_label = m_reduced_label;
        }
}

void nested_dissection::compute_separator(PartitionConfig &config, graph_access &G) {
        // set up the graph and config for computing a node separator
        config.k = 2;
        G.set_partition_count(config.k + 1);
        config.mode_node_separators = true;
        config.graph_allready_partitioned = false;
        balance_configuration bc;
        bc.configurate_balance(config, G);

        if (config.threads > 0 && (config.sep_portfolio > 1 || config.sep_metis_candidate)) {
                portfolio_separator(config, G);
                return;
        }

        // compute the separator
        area_bfs::m_deepth.resize(G.number_of_nodes(), 0);
        forall_nodes(G, node) {
                area_bfs::m_deepth[node] = 0;
        } endfor

        graph_partitioner partitioner;
        partitioner.perform_partitioning(config, G);
}

void nested_dissection::portfolio_separator(const PartitionConfig &config, graph_access &G) {
        // KaHIP's runs, then METIS's separator as the last when asked for
        const int runs = std::max(1, config.sep_portfolio) + (config.sep_metis_candidate ? 1 : 0);
        std::vector<int> seeds(runs);
        for (auto &s : seeds) s = random_functions::nextInt(0, std::numeric_limits<int>::max() - 1);
        std::vector<NodeWeight> weights(runs);
        std::vector<std::vector<PartitionID>> partitions(runs);
        std::vector<PartitionID> counts(runs), separator_blocks(runs);
        auto run = [&](int i) {
                random_functions::setSeed(seeds[i]);
                area_bfs::m_deepth.assign(G.number_of_nodes(), 0);
                graph_access copy;
                G.copy(copy);
                copy.set_partition_count(G.get_partition_count());
                if (config.sep_metis_candidate && i == runs - 1) {
                        metis_separator(copy, config.metis_nseps);
                } else {
                        PartitionConfig own = config;
                        if (config.sep_rating == -2) own.sep_rating = i % 4;
                        graph_partitioner partitioner;
                        partitioner.perform_partitioning(own, copy);
                }
                quality_metrics qm;
                weights[i] = qm.separator_weight(copy);
                counts[i] = copy.get_partition_count();
                separator_blocks[i] = copy.getSeparatorBlock();
                partitions[i].resize(copy.number_of_nodes());
                forall_nodes(copy, node) {
                        partitions[i][node] = copy.getPartitionIndex(node);
                } endfor
        };
        std::vector<std::thread> spawned;
        for (int i = 0; i + 1 < runs; i++) {
                if (thread_budget::take()) spawned.emplace_back(run, i);
                else run(i);
        }
        run(runs - 1);
        for (auto &t : spawned) {
                t.join();
                thread_budget::give();
        }

        int best = 0;
        for (int i = 1; i < runs; i++) if (weights[i] < weights[best]) best = i;
        G.set_partition_count(counts[best]);
        G.setSeparatorBlock(separator_blocks[best]);
        forall_nodes(G, node) {
                G.setPartitionIndex(node, partitions[best][node]);
        } endfor
}

void nested_dissection::dissect_children(const PartitionConfig &config, graph_access &G) {
        // The blocks in KaHIP's order, the parts and then the separator, each
        // with its first position and a seed drawn here in that order: every
        // task is then independent of which thread runs it, and when.
        std::vector<PartitionID> blocks;
        forall_blocks(G, p) {
                if (p != G.getSeparatorBlock()) blocks.push_back(p);
        } endfor
        blocks.push_back(G.getSeparatorBlock());
        std::vector<NodeID> size(G.get_partition_count(), 0);
        forall_nodes(G, node) {
                size[G.getPartitionIndex(node)]++;
        } endfor
        std::vector<NodeID> begin(blocks.size());
        NodeID at = 0;
        for (size_t i = 0; i < blocks.size(); i++) {
                begin[i] = at;
                at += size[blocks[i]];
        }
        std::vector<int> seeds(blocks.size());
        for (auto &s : seeds) s = random_functions::nextInt(0, std::numeric_limits<int>::max() - 1);

        // Each task writes its own range of m_reduced_label and works on its
        // own configuration; G is only read.
        auto task = [&](size_t i) {
                random_functions::setSeed(seeds[i]);
                PartitionConfig own = config;
                NodeID first = begin[i];
                recurse_dissection(own, G, blocks[i], first);
        };
        std::vector<std::thread> spawned;
        for (size_t i = 0; i + 1 < blocks.size(); i++) {
                if (thread_budget::take()) spawned.emplace_back(task, i);
                else task(i);
        }
        task(blocks.size() - 1);
        for (auto &t : spawned) {
                t.join();
                thread_budget::give();
        }
}

void nested_dissection::recurse_dissection(PartitionConfig &config, graph_access &G, PartitionID block, NodeID &order_begin) {
        std::vector<NodeID> mapping;
        graph_extractor extractor;
        graph_access subgraph;
        extractor.extract_block(G, subgraph, block, mapping);
        nested_dissection dissection(&subgraph, m_recursion_level + 1);
        dissection.perform_nested_dissection(config);

        // Transfer labels from the subgraph to the reduced graph
        for (size_t i = 0; i < mapping.size(); ++i) {
                m_reduced_label[mapping[i]] = dissection.m_label[i] + order_begin;
        }
        order_begin += mapping.size();
}

const std::vector<NodeID>& nested_dissection::ordering() const {
        return m_label;
}
