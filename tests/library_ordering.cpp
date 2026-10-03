#include <algorithm>
#include <iostream>
#include <vector>

#include "check.h"
#include "kaHIP_interface.h"

int main() {
        try {
                const int side = 16;
                int n = side * side;
                std::vector<kahip_idx> xadj(n + 1), adjncy;
                for (int y = 0; y < side; ++y) {
                        for (int x = 0; x < side; ++x) {
                                const int v = y * side + x;
                                xadj[v] = static_cast<kahip_idx>(adjncy.size());
                                if (x > 0) adjncy.push_back(v - 1);
                                if (x + 1 < side) adjncy.push_back(v + 1);
                                if (y > 0) adjncy.push_back(v - side);
                                if (y + 1 < side) adjncy.push_back(v + side);
                        }
                }
                xadj[n] = static_cast<kahip_idx>(adjncy.size());
                for (int mode : {FAST, ECO}) {
                        std::vector<int> first(n), repeated(n);
                        reduced_nd(&n, xadj.data(), adjncy.data(), true, 42, mode, first.data());
                        reduced_nd(&n, xadj.data(), adjncy.data(), true, 42, mode, repeated.data());
                        require(first == repeated, "the C API must reproduce an ordering for the same seed");
                        std::sort(first.begin(), first.end());
                        for (int v = 0; v < n; ++v) require(first[v] == v, "the C API must return a permutation");
                        std::cout << "mode=" << mode << ", permutation size=" << n << ", repeat equal=1\n";
                }
        } catch (const std::exception &error) {
                std::cerr << error.what() << '\n';
                return 1;
        }
}
