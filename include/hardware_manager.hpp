#ifndef DISTSIM_HW_MAN_HPP
#define DISTSIM_HW_MAN_HPP
#include <vector>
#include <functional>
#include <map>
#include <mutex>
#include <memory>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <iostream>
#include "concurrentqueue.hpp"
#include "common.hpp"
#include "node.hpp"
#include "message.hpp"
#include "rng.hpp"

template <typename T>
class HardwareManager {
private:
    const node_id_t max_id;
    const int nthreads;
    const uint64_t fail_thres;
    std::map<node_id_t, std::unique_ptr<Node<T>>> nodes;

    moodycamel::ConcurrentQueue<node_id_t> nodes_queue;
    std::atomic<bool> stopping;
    std::atomic<bool> pausing;
    std::atomic<int> running_threads;
    std::vector<std::thread> workers;
    std::uint64_t seed;

    /**
     * Computes the actual number of threads in function of nt.
     */
    int compute_nthreads(int nt) const {
        int nthreads = nt;
        if (nthreads == -1) nthreads = std::thread::hardware_concurrency();
        if (nthreads == 0) nthreads = 1;
        return nthreads;
    }
protected:
    /**
     * Generate a random id
     */
    node_id_t random_id() {
        return rng() % max_id;
    }

    template<typename node_t>
    void add_node(std::unique_ptr<node_t> ptr) {
        pause();
        auto id = ptr->id();
        {
            run_lock lck(this);
            nodes.emplace(id, std::move(ptr));
        }
        try {
            nodes[id]->init();
        } catch (std::exception& e) {
            std::cerr << "Error during init!" << std::endl;
        }
    }

public:
    HardwareManager(
        node_id_t max_id,
        int nt,
        uint64_t seed,
        double link_fail_chance = 0
    ): max_id(max_id), nthreads{compute_nthreads(nt)},
       fail_thres(link_fail_chance * std::numeric_limits<uint64_t>::max()),
       stopping(false), pausing(false), running_threads(0), seed(seed) {}

    class run_lock {
        HardwareManager* manager;
    public:
        run_lock(HardwareManager* manager): manager(manager) {
            manager->pause();
        }
        ~run_lock() {
            manager->resume();
        }
    };

    /**
     * Check if a can send to b
     */
    virtual bool can_send(node_id_t a, node_id_t b) const {
        return a != b;
    }

    /**
     * Iterate on n's neighbours, executing callback for every neighbour found.
     * If the callback returns true the iteration continues, otherwise
     * the iteration ends.
     */
    virtual void iter_neighbours(
        node_id_t n,
        const std::function<bool(node_id_t)>& callback
    ) const {
        for (auto& node: nodes) {
            if (can_send(n, node.second->id())) {
                if (!callback(node.second->id())) break;
            }
        }
    };

    /**
     * Return a vector containing n's neihbours.
     */
    virtual std::vector<node_id_t> get_neighbours(node_id_t n) const {
        std::vector<node_id_t> ans;
        iter_neighbours(n, [&ans] (node_id_t neigh) {
            ans.push_back(neigh);
            return true;
        });
        return ans;
    }

    /**
     * Return the number of n's neighbours.
     */
    virtual std::size_t count_neighbours(node_id_t n) const {
        std::size_t ans;
        iter_neighbours(n, [&ans] (node_id_t neigh) {
            ans++;
            return true;
        });
        return ans;
    }

    /**
     * Returns true if there is a node with id bigger than or equal to
     * the given one.
     */
    bool has_bigger_id(node_id_t i) const {
        return nodes.end() != nodes.lower_bound(i);
    }

    /**
     * Returns the next id bigger than or equal to the argument. Raises an
     * exception if there is none.
     */
    node_id_t next_id(node_id_t i) const {
        if (!has_bigger_id(i)) throw std::runtime_error("Invalid argument");
        return nodes.lower_bound(i)->first;
    }

    /**
     * Generates a message at a given node.
     */
    void gen_message(node_id_t sender, const T& data = T{}) {
        Node<T>* nd;
        if (!nodes.count(sender)) throw std::runtime_error("Invalid sender");
        nd = nodes.at(sender).get();
        try {
            nd->start_message(Message<T>{data});
        } catch (std::exception& e) {
            std::cerr << "Error during start_message!" << std::endl;
        }
    }

    /**
     * Gets read-only access to a given node.
     */
    const Node<T>* get(node_id_t node) const {
        if (!nodes.count(node)) throw std::runtime_error("Invalid node");
        return nodes.at(node).get();
    }

    /**
     * Forwards a message to a given node. Throws an exception if the sender
     * or the receiver do not exist or if the sender cannot send messages to
     * the receiver.
     */
    void send_message(node_id_t sender, node_id_t receiver, Message<T> msg) {
        if (rng() < fail_thres) return;
        if (!nodes.count(sender))
            throw std::runtime_error("Invalid sender");
        if (!nodes.count(receiver))
            throw std::runtime_error("Invalid receiver");
        if (!can_send(sender, receiver))
            throw std::runtime_error("The sender cannot send to the receiver!");
        msg.hops++;
        Node<T>* nd;
        nd = nodes.at(receiver).get();
        nd->enqueue(std::move(msg));
        nodes_queue.enqueue(receiver);
    }

    /**
     * Makes a node fail.
     */
    void fail(node_id_t node) {
        if (!nodes.count(node)) throw std::runtime_error("Invalid node");
        run_lock lck(this);
        nodes.erase(node);
    }

    /**
     * Add a single node.
     */
    template<typename node_t, typename... Args>
    void add_node(node_id_t id, Args... args) {
        add_node(std::make_unique<node_t>(this, id, args...));
    }

    /**
     * Generate a new id
     *
     * TODO: make this work when there are a lot of nodes
     */
    node_id_t gen_id() {
        if (4 * nodes.size() / 3 >= max_id) {
            throw std::runtime_error("Too many ids generated");
        }
        node_id_t newid = random_id();
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
    node_id_t get_random_node() {
        if (nodes.size() == 0) throw std::runtime_error("Empty node list");
        node_id_t id = random_id();
        while (!has_bigger_id(id)) {
            id = random_id();
        }
        return next_id(id);
    }

    /**
     * Starts handling messages.
     */
    void run() {
        stopping = false;
        pausing = false;
        auto workerfun = [&] (int thread_idx) {
            rng = xoroshiro(thread_idx+1, seed);
            running_threads++;
            while (true) {
                Node<T>* node;
                node_id_t node_idx;
                bool was_empty = false;
                while (!nodes_queue.try_dequeue(node_idx)) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                    if (stopping) {
                        was_empty = true;
                        break;
                    }
                }
                if (was_empty) break;
                node = nodes.at(node_idx).get();
                try {
                    int num = 0;
                    while (true) {
                        if (num++ > 128) {
                            nodes_queue.enqueue(node->id());
                            break;
                        }
                        int ret = node->handle_one_message();
                        if (ret == 0) break;
                        if (ret == 1) continue;
                        if (ret == -1) {
                            nodes_queue.enqueue(node->id());
                            break;
                        }
                        throw std::runtime_error("Invalid return value from handle_one_message");
                    }
                } catch (std::exception& e) {
                    std::cerr << e.what() << std::endl;
                }
                if (pausing) {
                    running_threads--;
                    while (pausing) {
                        using namespace std::literals::chrono_literals;
                        std::this_thread::sleep_for(10us);
                    }
                    running_threads++;
                }
            }
        };
        workers.clear();
        for (int i=0; i<nthreads; i++) {
            workers.emplace_back(workerfun, i);
        }
    }

    /**
     * Pauses the handling of messages.
     */
    void pause() {
        pausing = true;
        while (running_threads != 0) {
            using namespace std::literals::chrono_literals;
            std::this_thread::sleep_for(10us);
        }
    }

    /**
     * Resumes the handling of messages.
     */
    void resume() {
        pausing = false;
    }

    /**
     * Stop handling messages.
     */
    void stop() {
        stopping = true;
        for (int i=0; i<nthreads; i++) {
            workers[i].join();
        }
    }
    virtual ~HardwareManager() = default;
};

#endif
