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
#include "common.hpp"
#include "hardware_manager.hpp"
#include "message.hpp"

using namespace std::chrono;

template <typename T>
class Node {
public:
    friend class HardwareManager<T>;
    static std::atomic<long long> queued_messages;
    static std::atomic<long long> all_messages;
private:
    HardwareManager<T>* manager_;
    node_id_t id_;

    typedef std::pair<time_point<high_resolution_clock>, Message<T>> p_msg_t;
    // Simple queue for undelayed messages
    std::queue<Message<T>> messages;
    // Min-heap for delayed messages
    std::priority_queue<p_msg_t, std::vector<p_msg_t>, std::greater<p_msg_t>> delayed_messages;
    std::mutex messages_mutex;

    /**
     * Gets a message from the queue and dispatches it to handle_message.
     * If both queues are empty, or if the delayed messages queue only has messages
     * that should be delivered in the future, do nothing.
     *
     * @return 1 if a message was handled, 0 if the queues were empty and -1 if
     *          there was an enqueued message but it should not be received yet.
     */
    int handle_one_message() {
        OPTNS::optional<Message<T>> msg;
        auto now = high_resolution_clock::now();
        {
            std::lock_guard<std::mutex> lck{messages_mutex};
            if (messages.size() != 0) {
                msg = std::move(messages.front());
                messages.pop();
            } else if (delayed_messages.size() != 0) {
                if (delayed_messages.top().first > now) return -1;
                msg = delayed_messages.top().second;
                delayed_messages.pop();
                queued_messages--;
            } else return 0;
        }
        handle_message(std::move(msg.value()));
        return 1;
    }

protected:
    /**
     * Creates a new message's content and (possibly) sends it.
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
    node_id_t id() const {return id_;}

    /**
     * Gets the node's hardware manager.
     */
    HardwareManager<T>& manager() {return *manager_;}

    /**
     * Constructor
     */
    Node(HardwareManager<T>* manager, node_id_t id): manager_(manager), id_(id) {}

     /**
     * Adds a message to the queue. If the message cannot be enqueued,
     * it is lost.
     */
    void enqueue(Message<T> msg) {
        auto now = high_resolution_clock::now();
        std::lock_guard<std::mutex> lck{messages_mutex};
        if (check_enqueue()) {
            if (msg.delay().count() == 0) {
                messages.push(msg);
            } else {
                queued_messages++;
                all_messages++;
                delayed_messages.emplace(now + msg.delay(), msg);
            }
        }
    }
public:
    virtual ~Node() = default;
};

#endif
