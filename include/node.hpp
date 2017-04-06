#ifndef DISTSIM_NODE_HPP
#define DISTSIM_NODE_HPP
#include <mutex>
#ifdef __has_include
# if __has_include(<optional>)
#  include <optional>
#define OPTNS std
# elif __has_include(<experimental/optional>)
#  include <experimental/optional>
#define OPTNS std::experimental
# else
#  error "You need at least one of <optional> and <experimental/optional>!"
# endif
#else
#include <experimental/optional>
#define OPTNS std::experimental
#endif
#include <queue>
#include "hardware_manager.hpp"
#include "message.hpp"

template <typename T>
class Node {
public:
    typedef typename HardwareManager<T>::id_t id_t;
    friend class HardwareManager<T>;
private:
    HardwareManager<T>* manager_;
    id_t id_;

    std::queue<Message<T>> messages;
    std::mutex messages_mutex;

    // ensures that only one thread is running this node at the same time
    std::mutex m;

    /**
     * Gets a message from the queue and dispatches it to handle_message.
     * If the queue is empty, do nothing.
     */
    bool handle_one_message() {
        OPTNS::optional<Message<T>> msg;
        {
            std::lock_guard<std::mutex> lck{messages_mutex};
            if (messages.size() == 0) return false;
            msg = std::move(messages.front());
            messages.pop();
        }
        handle_message(std::move(msg.value()));
        return true;
    }

    /**
     * Return the node's mutex
     */
    std::mutex& get_mutex() {return m;}

protected:
    /**
     * Creates a new message's content and sends it
     */
    virtual void start_message(Message<T> msg) = 0;

    /**
     * Checks if we can enqueue a message
     */
    virtual bool check_enqueue() {
        return true;
    }

    /**
     * Handles a single message
     */
    virtual void handle_message(Message<T> msg) = 0;

    /**
     * Called once when the node is created and inserted in the network
     */
    virtual void init() {}

    /**
     * Gets the node's id.
     */
    id_t id() const {return id_;}

    /**
     * Gets the node's hardware manager.
     */
    HardwareManager<T>& manager() {return *manager_;}

    /**
     * Constructor
     */
    Node(HardwareManager<T>* manager, id_t id): manager_(manager), id_(id) {}

     /**
     * Adds a message to the queue
     */
    void enqueue(Message<T> msg) {
        std::lock_guard<std::mutex> lck{messages_mutex};
        if (check_enqueue())
            messages.push(msg);
    }
};

#endif
