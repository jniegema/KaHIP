#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#ifndef _WIN32
#include <regex.h>
#endif
#include <argtable3.h>

#include "check.h"
#include "parse_parameters.h"

int main() {
        try {
                const char *presets[] = {"fast", "eco", "strong"};
                for (const char *preset : presets) {
                        for (PartitionID previous_k : {2u, 16u, 1024u}) {
                                PartitionConfig config;
                                // Initialized sentinels make the missing default detectable without
                                // relying on a particular stack layout or an uninitialized read.
                                config.k = previous_k;
                                std::vector<std::string> arguments = {"test", "unused.graph",
                                                                     std::string("--preconfiguration=") + preset};
                                std::vector<char *> argv;
                                for (auto &argument : arguments) argv.push_back(&argument[0]);
                                std::string filename;
                                bool weighted = false, suppressed = false, recursive = false;
                                require(parse_parameters(static_cast<int>(argv.size()), argv.data(), config,
                                                         filename, weighted, suppressed, recursive) == 0,
                                        "valid separator arguments must parse");
                                std::cout << preset << ": k=" << config.k
                                          << ", initial FM limit=" << config.bipartition_post_fm_limits << '\n';
                                require(config.k == 2, "a separator must bisect independently of the previous k");
                                require(config.bipartition_post_fm_limits == 30, "presets must see k=2 before choosing the FM limit");
                                if (std::strcmp(preset, "fast") == 0) {
                                        require(!config.quotient_graph_refinement_disabled, "fast must not select the large-k refinement branch");
                                }
                                if (std::strcmp(preset, "eco") == 0) {
                                        require(config.aggressive_random_levels == 6 && config.kway_rounds == 1,
                                                "eco's logarithmic limits must use k=2");
                                }
                        }
                }
        } catch (const std::exception &error) {
                std::cerr << error.what() << '\n';
                return 1;
        }
}
