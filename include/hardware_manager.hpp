#ifndef DISTSIM_HW_MAN_HPP
#define DISTSIM_HW_MAN_HPP
#include <vector>
#include <functional>
#include <map>
#include <mutex>
#include <memory>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <iostream>
#include "node.hpp"
#include "message.hpp"
#include "rng.hpp"

template <typename T>
class Node;

template <typename T>
class HardwareManager {
public:
    typedef std::size_t id_t;
    typedef void(callback_fun)(const Node<T>* n, Message<T>);
private:
    const id_t max_id;
    const uint64_t fail_thres;
    std::map<id_t, std::shared_ptr<Node<T>>> nodes;
    mutable std::mutex nodes_m;

    mutable std::mutex queue_m;
    std::condition_variable queue_empty;
    std::queue<id_t> nodes_queue;
    std::atomic<bool> stopping;
    std::vector<std::thread> workers;
    std::function<callback_fun> complete_callback_;

    xoroshiro rng;
    
protected:
    /**
     * Generate a random id
     */
    id_t random_id() {
        return rng() % max_id;
    }

    /**
     * Check if a can send to b
     */
    virtual bool can_send_(id_t a, id_t b) const {
        return a != b;
    }

public:
    HardwareManager(
        id_t max_id,
        std::function<callback_fun> complete_callback,
        double link_fail_chance = 0
    ): max_id(max_id),
       fail_thres(link_fail_chance * std::numeric_limits<uint64_t>::max()),
       stopping(false), complete_callback_(complete_callback) {}

    /**
     * Check if a can send to b
     */
    bool can_send(id_t a, id_t b) const {
        std::lock_guard<std::mutex> l(nodes_m);
        return a != b;
    }

    /**
     * Iterate on n's neighbours, executing callback for every neighbour found.
     * If the callback returns true the iteration continues, otherwise
     * the iteration ends.
     */
    virtual void iter_neighbours(
        id_t n,
        const std::function<bool(id_t)>& callback
    ) const {
        std::lock_guard<std::mutex> l(nodes_m);
        for (auto& node: nodes) {
            if (can_send(n, node.second->id())) {
                if (!callback(node.second->id())) break;
            }
        }
    };

    /**
     * Return a vector containing n's neihbours.
     */
    virtual std::vector<id_t> get_neighbours(id_t n) const {
        std::vector<id_t> ans;
        iter_neighbours(n, [&ans] (id_t neigh) {
            ans.push_back(neigh);
            return true;
        });
        return ans;
    }

    /**
     * Return the number of n's neighbours.
     */
    virtual std::size_t count_neighbours(id_t n) const {
        std::size_t ans;
        iter_neighbours(n, [&ans] (id_t neigh) {
            ans++;
            return true;
        });
        return ans;
    }

    /**
     * Returns true if there is a node with id bigger than or equal to
     * the given one.
     */
    bool has_bigger_id(id_t i) const {
        return nodes.end() != nodes.lower_bound(i);
    }

    /**
     * Returns the next id bigger than or equal to the argument. Raises an
     * exception if there is none.
     */
    id_t next_id(id_t i) const {
        std::lock_guard<std::mutex> l(nodes_m);
        if (!has_bigger_id(i)) throw std::runtime_error("Invalid argument");
        return nodes.lower_bound(i)->first;
    }

    /**
     * Generates a message at a given node.
     */
    void gen_message(id_t sender) {
        std::shared_ptr<Node<T>> nd;
        {
            std::lock_guard<std::mutex> l(nodes_m);
            if (!nodes.count(sender)) throw std::runtime_error("Invalid sender");
            nd = nodes.at(sender);
        }
        std::lock_guard<std::mutex> lck{nd->get_mutex()};
        try {
            nd->start_message(Message<T>{});
        } catch (std::exception& e) {
            std::cerr << "Error during start_message!" << std::endl;
        }
    }

    /**
     * Forwards a message to a given node. Throws an exception if the sender
     * or the receiver do not exist or if the sender cannot send messages to
     * the receiver.
     */
    void send_message(id_t sender, id_t receiver, Message<T> msg) {
        if (rng() < fail_thres) return;
        std::shared_ptr<Node<T>> nd;
        {
            std::lock_guard<std::mutex> l(nodes_m);
            if (!nodes.count(sender))
                throw std::runtime_error("Invalid sender");
            if (!nodes.count(receiver))
                throw std::runtime_error("Invalid receiver");
            nd = nodes.at(receiver);
        }
        if (!can_send(sender, receiver))
            throw std::runtime_error("The sender cannot send to the receiver!");
        msg.hops++;
        nd->enqueue(std::move(msg));
        std::lock_guard<std::mutex> lck(queue_m);
        nodes_queue.push(receiver);
        queue_empty.notify_one();
    }

    /**
     * Makes a node fail.
     */
    void fail(id_t node) {
        std::lock_guard<std::mutex> l(nodes_m);
        if (!nodes.count(node)) throw std::runtime_error("Invalid node");
        nodes.erase(node);
    }

    /**
     * Add a single node.
     */
    template<typename node_t, typename... Args>
    void add_node(id_t id, Args... args) {
        {
            std::lock_guard<std::mutex> l(nodes_m);
            nodes.emplace(id, std::make_shared<node_t>(this, id, args...));
        }
        std::lock_guard<std::mutex> lck{nodes.at(id)->get_mutex()};
        try {
            nodes.at(id)->init();
        } catch (std::exception& e) {
            std::cerr << "Error during init!" << std::endl;
        }
    }

    /**
     * Generate a new id
     *
     * TODO: make this work when there are a lot of nodes
     */
    id_t gen_id() {
        if (4 * nodes.size() / 3 >= max_id) {
            throw std::runtime_error("Too many ids generated");
        }
        id_t newid = random_id();
        while (nodes.count(newid)) {
            newid = random_id();
        }
        return newid;
    }

    /**
     * Returns a random node id. Raises an exception if there are no nodes.
     *
     * TODO: make this faster when there are very few nodes
     */
    id_t get_random_node() {
        if (nodes.size() == 0) throw std::runtime_error("Empty node list");
        id_t id = random_id();
        while (!has_bigger_id(id)) {
            id = random_id();
        }
        return next_id(id);
    }

    /**
     * Starts handling messages in nt threads.
     *
     * TODO: fix very high contention for multithreaded code
     */
    template<int nt = -1>
    void run() {
        int nthreads = nt;
        if (nthreads == -1) nthreads = std::thread::hardware_concurrency();
        if (nthreads == 0) nthreads = 1;
        stopping = false;
        auto workerfun = [&] () {
            while (!stopping) {
                std::shared_ptr<Node<T>> node;
                {
                    std::unique_lock<std::mutex> lck(queue_m);
                    while (!stopping && nodes_queue.empty()) {
                        queue_empty.wait(lck);
                    }
                    if (stopping) break;
                    std::lock_guard<std::mutex> l(nodes_m);
                    node = nodes.at(nodes_queue.front());
                    nodes_queue.pop();
                }
                try {
                    std::lock_guard<std::mutex> lck(node->get_mutex());
                    if (!node->handle_one_message()) {
                        std::cerr << "No message was present!" << std::endl;
                    }
                } catch (std::exception& e) {
                    std::cerr << e.what() << std::endl;
                }
            }
        };
        workers.clear();
        for (int i=0; i<nthreads; i++) {
            workers.emplace_back(workerfun);
        }
    }

    /**
     * Stop handling messages.
     */
    void stop() {
        stopping = true;
        for (auto& wrk: workers) {
            queue_empty.notify_all();
            wrk.join();
        }
    }

    /**
     * Returns the function that should be called when a message has reached
     * its destination;
     */
    const std::function<callback_fun>& complete_callback() const {
        return complete_callback_;
    };
};

#endif
