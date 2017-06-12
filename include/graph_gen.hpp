#ifndef DISTSIM_GRAPH_GEN_HPP
#define DISTSIM_GRAPH_GEN_HPP
#include <vector>
#include <cmath>
#include "rng.hpp"

typedef std::vector<std::pair<std::size_t, std::size_t>> edge_list_t;

/**
 * Generates a new random connected graph according to the Erdos-Renyi model (more or less).
 *
 * N, M and S are, respectively, the number of nodes, the number of edges and the seed to use.
 * If M is not at least N-1, it is increased accordingly.
 */
inline edge_list_t gen_conn_erdos(int N, int M, int S=0) {
    if (M < N-1) M = N-1;
    edge_list_t ans;
    for (int i=1; i<N; i++)
        ans.emplace_back(i, rng(i));
    std::vector<uint64_t> excluded;
    for (auto x: ans) excluded.push_back((uint64_t)x.first*(x.first-1)/2+x.second);
    std::sort(excluded.begin(), excluded.end());
    auto to_add = rng.get_distinct(M-N+1, (uint64_t)N*(N-1)/2, excluded);
    for (auto edg: to_add) {
        std::size_t first = round(sqrt(2*(edg+1)));
        std::size_t second = edg-first*(first-1)/2;
        ans.emplace_back(first, second);
    }
    return ans;
}

/**
 * Variant of Barabasi-Albert's algorithm to generate a scale-free network.
 *
 * N, K and S are, respectively, the number of nodes of the network,
 * a factor proportional to the connectivity of the network (choose K=1 to
 * have the original algorithm) and the seed.
 */
inline edge_list_t gen_barabasi_albert(int N, int K, int S=0) {
    edge_list_t ans;
    ans.emplace_back(1, 0);
    for (int i=2; i<N; i++) {
        std::vector<std::size_t> neighs;
        for (auto idx: rng.get_distinct(K, ans.size())) {
            neighs.emplace_back(ans[idx].first);
            neighs.emplace_back(ans[idx].second);
        }
        std::sort(neighs.begin(), neighs.end());
        neighs.erase(std::unique(neighs.begin(), neighs.end()), neighs.end());
        for (auto neigh: neighs) {
            ans.emplace_back(i, neigh);
        }
    }
    return ans;
}

#endif
