#include "graph_gen.hpp"
#include <string>
#include <iostream>
using namespace std::literals;


/**
 * Generates a graph and outputs it in CSV format.
 */

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " (erdos|barabasi) N (M|K) [S]" << std::endl;;
        return -1;
    }
    const char* type = argv[1];
    int N = atoi(argv[2]);
    int par = atoi(argv[3]);
    int S = argc > 4 ? atoi(argv[4]) : 0;
    edge_list_t edges;
    if (type == "erdos"s) edges = gen_conn_erdos(N, par, S);
    else if (type == "barabasi"s) edges = gen_barabasi_albert(N, par, S);
    else {
        std::cerr << "Unknown graph type " << type << "! Valid types are: erdos, barabasi" << std::endl;
        return -1;
    }
    for (auto edg: edges) {
        std::cout << edg.first << ";" << edg.second << std::endl;
    }
}
