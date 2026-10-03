//
// Author: Christian Schulz <christian.schulz.phone@gmail.com>
// 

#include <algorithm>
#include <atomic>
#include <limits>
#include "initial_node_separator.h"
#include "graph_partitioner.h"
#include "tools/quality_metrics.h"
#include "tools/random_functions.h"
#include "partition/uncoarsening/separator/vertex_separator_algorithm.h"
#include "partition/uncoarsening/refinement/node_separators/fm_ns_local_search.h"
#include "partition/uncoarsening/separator/area_bfs.h"
#include "tools/task_pool.h"

initial_node_separator::initial_node_separator() {
                
}

initial_node_separator::~initial_node_separator() {
                
}

NodeWeight initial_node_separator::single_run( const PartitionConfig & config, graph_access & G) {

        // Per-try redirection of the process-wide stream would race between workers.
        graph_partitioner partitioner;
        PartitionConfig partition_config         = config;
        partition_config.mode_node_separators    = false;
        partition_config.global_cycle_iterations = 1;
        partition_config.repetitions             = 1;

        //computing a partition
        partitioner.perform_partitioning(partition_config, G);

        complete_boundary boundary(&G);
        boundary.build();

        vertex_separator_algorithm vsa; std::vector<NodeID> separator;
        //create a very simple separator from that partition
        if( partition_config.sep_full_boundary_ip ) {
                vsa.compute_vertex_separator_simpler(partition_config, G, boundary, separator);
        } else {
                vsa.compute_vertex_separator_simple(partition_config, G, boundary, separator);
        }
 
        std::vector<NodeID> output_separator;
        //improve the separator using flow based techniques
        vsa.improve_vertex_separator(partition_config, G, separator, output_separator);

        quality_metrics qm;
        return qm.separator_weight(G);

}

void initial_node_separator::compute_node_separator( const PartitionConfig & config, graph_access & G) {
        if(config.graph_allready_partitioned) return;
        if(config.threads > 0) {
                seeded_tries(config, G);
                return;
        }

        std::vector< NodeID > best_separator(G.number_of_nodes(),0);
        NodeWeight best_separator_size = std::numeric_limits< NodeWeight >::max();

	int unsucc_counter = 0;

        for( int i  = 0; i  < config.max_initial_ns_tries; i++) {
                NodeWeight cur_separator_size = single_run(config, G);
                if(cur_separator_size < best_separator_size) {
                        forall_nodes(G, node) {
                                best_separator[node] = G.getPartitionIndex(node);
                        } endfor
                        best_separator_size = cur_separator_size;
                
                        PRINT(std::cout <<  "improved initial separator size " <<  cur_separator_size  << std::endl;)
			unsucc_counter = 0;
                } else {
			unsucc_counter++;
		}

		if( config.faster_ns && unsucc_counter >= 5) {
			break;
		}
        }

        forall_nodes(G, node) {
                G.setPartitionIndex(node, best_separator[node]);
        } endfor
        
}

void initial_node_separator::seeded_tries( const PartitionConfig & config, graph_access & G) {
        const int tries = std::max(1, config.max_initial_ns_tries);
        std::vector<int> seeds(tries);
        for (auto &s : seeds) s = random_functions::nextInt(0, std::numeric_limits<int>::max() - 1);
        struct candidate {
                NodeWeight weight = std::numeric_limits<NodeWeight>::max();
                int index = std::numeric_limits<int>::max();
                PartitionID count = 0, separator_block = 2;
                std::vector<PartitionID> partition;
        };
        const int workers = std::min(tries, task_pool::concurrency());
        std::vector<candidate> best(workers);
        std::atomic<size_t> next(0);
        auto work = [&](int worker_index) {
                area_bfs::scoped_task state;
                graph_access copy;
                candidate &winner = best[worker_index];
                for (size_t i = next++; i < static_cast<size_t>(tries); i = next++) {
                        random_functions::setSeed(seeds[i]);
                        area_bfs::m_deepth.assign(G.number_of_nodes(), 0);
                        G.copy(copy);
                        copy.set_partition_count(G.get_partition_count());
                        copy.setSeparatorBlock(G.getSeparatorBlock());
                        initial_node_separator worker;
                        const NodeWeight weight = worker.single_run(config, copy);
                        if (weight < winner.weight || (weight == winner.weight && i < static_cast<size_t>(winner.index))) {
                                winner.weight = weight;
                                winner.index = static_cast<int>(i);
                                winner.count = copy.get_partition_count();
                                winner.separator_block = copy.getSeparatorBlock();
                                winner.partition.resize(copy.number_of_nodes());
                                forall_nodes(copy, node) {
                                        winner.partition[node] = copy.getPartitionIndex(node);
                                } endfor
                        }
                }
        };
        task_pool::group tasks;
        for (int w = 1; w < workers; w++) tasks.run([&, w] { work(w); });
        work(0);
        tasks.wait();

        const candidate &winner = *std::min_element(best.begin(), best.end(), [](const candidate &a, const candidate &b) {
                return a.weight < b.weight || (a.weight == b.weight && a.index < b.index);
        });
        G.set_partition_count(winner.count);
        G.setSeparatorBlock(winner.separator_block);
        forall_nodes(G, node) {
                G.setPartitionIndex(node, winner.partition[node]);
        } endfor
}
