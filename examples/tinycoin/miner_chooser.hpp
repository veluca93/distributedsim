#ifndef TINYCOIN_MINER_HPP
#define TINYCOIN_MINER_HPP
#include <set>
#include "graph_gen.hpp"
#include "rng.hpp"

auto choose_miners(int N, int num_honest, int num_selfish, const edge_list_t& edges, std::string algo, uint64_t seed) {
    std::vector<uint64_t> selfish;
    std::vector<uint64_t> honest;
    if (algo == "random") {
        selfish = rng.get_distinct(num_selfish, N);
    } else if (algo == "highdegree") {
        std::vector<std::pair<uint64_t, uint64_t>> degree(N, {0, 0});
        for (auto edg: edges) {
            degree[edg.first].first++;
            degree[edg.second].first++;
        }
        for (int i=0; i<N; i++) degree[i].second = i;
        std::sort(degree.begin(), degree.end(), std::greater<std::pair<uint64_t, uint64_t>>());
        for (int i=0; i<num_selfish; i++) selfish.push_back(degree[i].first);
    } else {
        std::cerr << "Unknown algorithm " << algo << "! Valid types are: random, highdegree" << std::endl;
        exit(-1);
    }
    honest = rng.get_distinct(num_honest, N, selfish);
    return std::make_pair(std::set<uint64_t>(honest.begin(), honest.end()), std::set<uint64_t>(selfish.begin(), selfish.end()));
}

#endif
