#ifndef NODE_ORDERING_CONFIG_H
#define NODE_ORDERING_CONFIG_H

#include <cstdint>
#include <cmath>
#include <limits>
#include <stdexcept>
#include "partition/partition_config.h"

inline bool exceeds_ordering_degree(EdgeID edges, NodeID nodes, unsigned int degree) {
        return degree > 0 && static_cast<uint64_t>(edges) > static_cast<uint64_t>(degree) * nodes;
}

inline void validate_node_ordering_config(const PartitionConfig &config) {
        if (config.threads < 0) throw std::runtime_error("threads must be nonnegative (0 selects serial ordering)");
        if (!std::isfinite(config.imbalance) || config.imbalance < 0)
                throw std::runtime_error("imbalance must be finite and nonnegative");
        if (config.faster_ns && config.threads > 0)
                throw std::runtime_error("sep_faster_ns requires threads=0; threaded ordering evaluates all initial tries");
        if (config.metis_depth < -1) throw std::runtime_error("metis_depth must be -1 or nonnegative");
        if (config.metis_nseps < 1) throw std::runtime_error("metis_nseps must be positive");
        if (config.sep_portfolio < 1 || config.sep_portfolio == std::numeric_limits<int>::max())
                throw std::runtime_error("sep_portfolio must be between 1 and INT_MAX-1");
        if (config.sep_rating < -2 || config.sep_rating > 3)
                throw std::runtime_error("sep_rating must be -2 (cycle), -1 (random), or 0, 1, 2, 3");
        if (config.dissection_rec_limit < 1 || config.sep_num_vert_stop < 2 || config.max_initial_ns_tries < 1)
                throw std::runtime_error("dissection_rec_limit and max_initial_ns_tries must be positive; sep_num_vert_stop must be at least 2");
        if (config.sep_num_fm_reps < 0 || config.sep_fm_unsucc_steps < 0 || config.max_flow_improv_steps < 0)
                throw std::runtime_error("separator refinement limits must be nonnegative");
        const bool portfolio = config.sep_portfolio > 1 || config.sep_metis_candidate;
        if (portfolio && config.threads == 0)
                throw std::runtime_error("sep_portfolio and sep_metis_candidate require threads > 0 (use threads=1 for one worker)");
        if ((config.sep_stop_cycle || config.sep_deeper_on_tie || config.sep_rating == -2) && !portfolio)
                throw std::runtime_error("sep_stop_cycle, sep_deeper_on_tie and sep_rating=-2 require a separator portfolio");
        if (config.sep_deeper_on_tie && config.metis_depth < 0)
                throw std::runtime_error("sep_deeper_on_tie requires metis_depth >= 0");
        if (config.sep_stop_cycle && config.sep_num_vert_stop > std::numeric_limits<int>::max() / 2)
                throw std::runtime_error("sep_num_vert_stop must be at most INT_MAX/2 with sep_stop_cycle");
        const bool metis_ordering = config.metis_below > 0 || config.metis_depth >= 0 || config.metis_above_degree > 0;
        if (config.metis_split > 0 && !metis_ordering)
                throw std::runtime_error("metis_split requires metis_below, metis_depth or metis_above_degree");
        if (config.metis_nseps != 1 && !metis_ordering && !config.sep_metis_candidate)
                throw std::runtime_error("metis_nseps requires METIS ordering or sep_metis_candidate");
#ifndef USEMETIS
        if (metis_ordering || config.sep_metis_candidate)
                throw std::runtime_error("METIS ordering options require a build with METIS");
#endif
}

#endif
