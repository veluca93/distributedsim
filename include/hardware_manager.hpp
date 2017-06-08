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
#include "common.hpp"
#include "node.hpp"
#include "message.hpp"
#include "rng.hpp"

template <typename T>
class HardwareManager {
public:
    typedef std::size_t id_t;
private:
    const id_t max_id;
    const int nthreads;
    const uint64_t fail_thres;
    std::map<id_t, std::unique_ptr<Node<T>>> nodes;

    mutable std::vector<std::mutex> queue_m;
    std::vector<std::queue<id_t>> nodes_queue;
    std::vector<std::condition_variable> queue_cv;
    std::atomic<bool> stopping;
    std::atomic<bool> pausing;
    std::atomic<int> running_threads;
    std::vector<std::thread> workers;

    static uint64_t rng() {
        thread_local xoroshiro rng_;
        return rng_();
    }

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
    id_t random_id() {
        return rng() % max_id;
    }

public:
    HardwareManager(
        id_t max_id,
        int nt,
        double link_fail_chance = 0
    ): max_id(max_id), nthreads{compute_nthreads(nt)},
       fail_thres(link_fail_chance * std::numeric_limits<uint64_t>::max()),
       queue_m(nthreads), nodes_queue(nthreads), queue_cv(nthreads),
       stopping(false), pausing(false), running_threads(0) {}

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
    bool can_send(id_t a, id_t b) const {
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
        if (!has_bigger_id(i)) throw std::runtime_error("Invalid argument");
        return nodes.lower_bound(i)->first;
    }

    /**
     * Generates a message at a given node.
     */
    void gen_message(id_t sender) {
        Node<T>* nd;
        if (!nodes.count(sender)) throw std::runtime_error("Invalid sender");
        nd = nodes.at(sender).get();
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
        uint64_t idx = receiver % nthreads;
        std::lock_guard<std::mutex> lck(queue_m[idx]);
        nodes_queue[idx].push(receiver);
        queue_cv[idx].notify_one();
    }

    /**
     * Makes a node fail.
     */
    void fail(id_t node) {
        if (!nodes.count(node)) throw std::runtime_error("Invalid node");
        run_lock lck(this);
        nodes.erase(node);
    }

    /**
     * Add a single node.
     */
    template<typename node_t, typename... Args>
    void add_node(id_t id, Args... args) {
        pause();
        {
            run_lock lck(this);
            nodes.emplace(id, std::make_unique<node_t>(this, id, args...));
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
     * Starts handling messages.
     */
    void run() {
        stopping = false;
        pausing = false;
        auto workerfun = [&] (int thread_idx) {
            running_threads++;
            while (!stopping) {
                Node<T>* node;
                {
                    std::unique_lock<std::mutex> lck(queue_m[thread_idx]);
                    while (!stopping && nodes_queue[thread_idx].empty()) {
                        queue_cv[thread_idx].wait(lck);
                    }
                    if (stopping) break;
                    node = nodes.at(nodes_queue[thread_idx].front()).get();
                    nodes_queue[thread_idx].pop();
                }
                try {
                    while (true) {
                        std::lock_guard<std::mutex> lck(node->get_mutex());
                        int ret = node->handle_one_message();
                        if (ret == 0) break;
                        if (ret == 1) continue;
                        if (ret == -1) {
                            // Re-enqueue the node for a later execution
                            std::unique_lock<std::mutex> lck(queue_m[thread_idx]);
                            nodes_queue[thread_idx].push(node->id());
                            queue_cv[thread_idx].notify_one();
                            continue;
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
            queue_cv[i].notify_all();
            workers[i].join();
        }
    }
};

#endif
