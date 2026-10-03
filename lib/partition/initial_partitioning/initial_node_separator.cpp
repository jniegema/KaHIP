//
// Author: Christian Schulz <christian.schulz.phone@gmail.com>
// 

#include <atomic>
#include <fstream>
#include <limits>
#include <thread>
#include "initial_node_separator.h"
#include "graph_partitioner.h"
#include "tools/quality_metrics.h"
#include "tools/random_functions.h"
#include "partition/uncoarsening/separator/vertex_separator_algorithm.h"
#include "partition/uncoarsening/refinement/node_separators/fm_ns_local_search.h"
#include "partition/uncoarsening/separator/area_bfs.h"
#include "tools/thread_budget.h"

initial_node_separator::initial_node_separator() {
                
}

initial_node_separator::~initial_node_separator() {
                
}

NodeWeight initial_node_separator::single_run( const PartitionConfig & config, graph_access & G) {

        // The partitioner's progress output is compiled out (PRINT) unless
        // KAFFPAOUTPUT is defined; redirecting std::cout to an opened null
        // device for every try silenced nothing else and is a race once the
        // tries run on threads (--threads).
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
        std::vector<NodeWeight> sizes(tries);
        std::vector<std::vector<PartitionID>> partitions(tries);
        // What a try leaves on its graph besides the indices: the serial code
        // runs on G itself, and uncoarsening carries G's partition count up.
        std::vector<PartitionID> counts(tries), separator_blocks(tries);
        std::atomic<int> next(0);
        // Workers only: the calling thread's generator must not depend on
        // which tries it would have run.
        auto work = [&]() {
                for (int i = next++; i < tries; i = next++) {
                        random_functions::setSeed(seeds[i]);
                        area_bfs::m_deepth.assign(G.number_of_nodes(), 0);
                        graph_access copy;
                        G.copy(copy);
                        copy.set_partition_count(G.get_partition_count());
                        copy.setSeparatorBlock(G.getSeparatorBlock());
                        initial_node_separator worker;
                        sizes[i] = worker.single_run(config, copy);
                        counts[i] = copy.get_partition_count();
                        separator_blocks[i] = copy.getSeparatorBlock();
                        partitions[i].resize(copy.number_of_nodes());
                        forall_nodes(copy, node) {
                                partitions[i][node] = copy.getPartitionIndex(node);
                        } endfor
                }
        };
        int workers = 1; // the calling thread's own, while it waits
        while (workers < tries && thread_budget::take()) workers++;
        std::vector<std::thread> pool;
        for (int w = 0; w < workers; w++) pool.emplace_back(work);
        for (auto &t : pool) t.join();
        for (int w = 1; w < workers; w++) thread_budget::give();

        int best = 0;
        for (int i = 1; i < tries; i++) if (sizes[i] < sizes[best]) best = i;
        G.set_partition_count(counts[best]);
        G.setSeparatorBlock(separator_blocks[best]);
        forall_nodes(G, node) {
                G.setPartitionIndex(node, partitions[best][node]);
        } endfor
}
