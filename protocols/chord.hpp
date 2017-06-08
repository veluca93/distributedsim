#ifndef DISTSIM_CHORD_HPP
#define DISTSIM_CHORD_HPP
#include "node.hpp"
#include "rng.hpp"

class ChordNode: public Node<std::size_t> {
    typedef std::function<void(const Node<std::size_t>* n, Message<std::size_t>)> (callback_fun);
    uint64_t bits;
    xoroshiro rng;
    const callback_fun& cb;
    friend class HardwareManager<std::size_t>;
    id_t distance(id_t other) {
        other %= 1ULL<<bits;
        if (other >= id()) return other - id();
        return (1ULL<<bits) + other - id();
    }
protected:
    /**
     * Returns the successor of a given id
     */
    id_t successor(id_t id) {
        id %= 1ULL<<bits;
        if (manager().has_bigger_id(id)) return manager().next_id(id);
        return manager().next_id(0);
    }

    /**
     * Implements Chord's routing algorithm
     */
    virtual void handle_message(Message<id_t> msg) override {
        id_t dst = successor(msg.data());
        if (id() == dst) {
            cb(this, msg);
            return;
        }
        for (unsigned i=bits; i>0; i--) {
            id_t finger = successor(id() + (1ULL<<(i-1)));
            if (distance(finger) <= distance(dst)) {
                manager().send_message(id(), finger, msg);
                break;
            }
        }
    }

    /**
     * Creates a new message's content and sends it
     */
    virtual void start_message(Message<id_t> msg) override {
        msg.data() = rng() % (1ULL<<bits);
        while (successor(msg.data()) == id()) {
            msg.data() = rng() % (1ULL<<bits);
        }
        handle_message(msg);
    };

public:
    ChordNode(HardwareManager<id_t>* manager, id_t id, uint64_t bits, const callback_fun& cb):
        Node(manager, id), bits(bits), cb(cb) {}
};

#endif
