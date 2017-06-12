#ifndef DISTSIM_GRAPH_HWM_HPP
#define DISTSIM_GRAPH_HWM_HPP
#include "hardware_manager.hpp"
#include "cuckoo.hpp"

template <typename T, bool directed = false>
class GraphHardwareManager: public HardwareManager<T> {
private:
    std::vector<cuckoo_hash_set<node_id_t>> graph;
public:
    GraphHardwareManager(int nt, uint64_t seed): HardwareManager<T>(0, nt, seed) {}
    /**
     * Check if a can send to b
     */
    bool can_send(node_id_t a, node_id_t b) const override {
        return graph[a].count(b);
    }

    /**
     * Iterate on n's neighbours, executing callback for every neighbour found.
     * If the callback returns true the iteration continues, otherwise
     * the iteration ends.
     */
    void iter_neighbours(
        node_id_t n,
        const std::function<bool(node_id_t)>& callback
    ) const override {
        for (auto& node: graph[n]) {
            if (!callback(node)) break;
        }
    };

    /**
     * Generate a new id. As a new node gets its ID as the number of
     * nodes present in the graph when it was inserted, this function
     * should not be used.
     */
    node_id_t gen_id() {
        throw std::runtime_error("Illegal function call");
    }

    /**
     * Returns a random node id.
     */
    node_id_t get_random_node() {
        return rng() % graph.size();
    }

    /**
     * Add a single node.
     */
    template<typename node_t, typename... Args>
    void add_node(Args... args) {
        HardwareManager<T>::add_node(std::move(std::make_unique<node_t>(this, graph.size(), args...)));
        graph.emplace_back();
    }

    /**
     * Add a single edge. The edge goes from a to be if the graph is directed,
     * in both directions otherwise.
     */
    void add_edge(node_id_t a, node_id_t b) {
        if (a >= graph.size() || b >= graph.size()) throw std::runtime_error("Invalid node in edge!");
        graph[a].insert(b);
        if (!directed) graph[b].insert(a);
    }
};

#endif
