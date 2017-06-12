#ifndef DISTSIM_TINYCOIN_HPP
#define DISTSIM_TINYCOIN_HPP
#include "common.hpp"
#include "node.hpp"
#include <atomic>
#include <vector>
#include <chrono>
#include <variant>
#include <cmath>
#include <set>
#include <assert.h>

struct TinyTransaction {
    static std::chrono::nanoseconds delay;
    static std::size_t next_id() {
        static std::atomic<std::size_t> n{1};
        return n++;
    };
    node_id_t source_node;
    node_id_t destination_node;
    double amount;
    size_t id;
    bool operator<(const TinyTransaction& other) const {
        return id < other.id;
    }
    TinyTransaction(node_id_t source_node, node_id_t destination_node, double amount):
        source_node(source_node), destination_node(destination_node), amount(amount),
        id(next_id()) {}
    TinyTransaction(): id(-1) {}
};

struct TinyBlock {
    static std::chrono::nanoseconds delay_per_transaction;
    static std::chrono::nanoseconds base_delay;
    static std::size_t next_id() {
        static std::atomic<std::size_t> n{1};
        return n++;
    };
    std::size_t id;
    std::size_t parent;
    node_id_t miner;
    std::shared_ptr<std::vector<TinyTransaction>> transactions;
    TinyBlock(std::size_t parent, node_id_t miner):
        id(next_id()), parent(parent), miner(miner),
        transactions(std::make_shared<std::vector<TinyTransaction>>()) {}
    TinyBlock(): id(-1) {}
    std::chrono::nanoseconds delay() const {
        return transactions->size()*delay_per_transaction+base_delay;
    }
};

using TinyData = std::variant<TinyTransaction, TinyBlock>;

class TinyNode: public Node<TinyData> {
protected:
    std::vector<TinyBlock> blockchain{1, {0, (node_id_t)-1}};
    std::vector<std::vector<TinyBlock>> pending_blocks;
    std::vector<TinyTransaction> received_transactions;
    std::vector<std::size_t> lengths{1, 0};
    std::mutex transaction_mutex;
    std::mutex blockchain_mutex;
    size_t head = 0;
    double balance;

    /**
     * Sets the data of a message, changing the delay to the correct value.
     *
     * Transaction overload.
     */
    void set_data(Message<TinyData>& msg, const TinyTransaction& tx) {
        msg.data(TinyData{tx});
        msg.delay(TinyTransaction::delay);
    }

    /**
     * Sets the data of a message, changing the delay to the correct value.
     *
     * Block overload.
     */
    void set_data(Message<TinyData>& msg, const TinyBlock& blk) {
        msg.data(TinyData{blk});
        msg.delay(blk.delay());
    }

    /**
     * Computes the value of a block for the current node.
     */
    double block_value(const TinyBlock& blk) {
        double val = 0;
        for (auto tx: *blk.transactions)
            if (tx.destination_node == id())
                val += tx.amount;
        if (blk.miner == id())
            val += block_reward + transaction_reward*blk.transactions->size();
        return val;
    }

    /**
     * Gets called whenever we confirm a block.
     */
    virtual void confirm(const TinyBlock& blk) {
        balance += block_value(blk);
    }

    /**
     * Gets called whenever we unconfirm a block.
     */
    virtual void unconfirm(const TinyBlock& blk) {
        balance -= block_value(blk);
    }

    /**
     * Update data that changes when we change head.
     * We assume that nodes do not try to double-spend.
     * We update the amount of money we have when a transaction
     * that has the current node as target is verified.
     */
    void update_head(size_t new_head) {
        size_t old_head = head;
        head = new_head;
        for (; lengths[new_head] > lengths[old_head]; new_head = blockchain[new_head].parent) {
            confirm(blockchain[new_head]);
        }
        for (; new_head != old_head; new_head = blockchain[new_head].parent, old_head = blockchain[old_head].parent) {
            confirm(blockchain[new_head]);
            unconfirm(blockchain[old_head]);
        }
    }

    /**
     * Handles a received block. Returns true if the block
     * was new, false if it was already there.
     */
    virtual bool handle_block(const TinyBlock& block) {
        bool should_forward = true;
        std::vector<TinyBlock> fwd;
        {
            std::lock_guard<std::mutex> blockchain_lock(blockchain_mutex);
            // If I have already seen this block, I should not forward it.
            if (satisfies<TinyBlock>(blockchain, block.id, [&](const TinyBlock& blk) {
                return blk.id != (std::size_t)-1;
            })) should_forward = false;

            // If I have already handled this block, do nothing.
            if (satisfies<TinyBlock>(blockchain, block.id, [&](const TinyBlock& blk) {
                return blk.id == block.id;
            })) return should_forward;

            vec_set(blockchain, block.id, block);
            if (pending_blocks.size() <= block.id) pending_blocks.resize(block.id+1);
            // If I have not handled the parent of this block yet, enqueue the block and
            // do nothing.
            if (satisfies<TinyBlock>(blockchain, block.parent, [&](const TinyBlock& blk) {
                return blk.id == (std::size_t)-1 || blk.id == (std::size_t)-2;
            })) {
                blockchain[block.id].id = -2;
                pending_blocks[block.parent].push_back(block);
                return should_forward;
            }
            vec_set(lengths, block.id, lengths[block.parent]+1);
            if (lengths[block.id] > lengths[head]) update_head(block.id);
            std::swap(fwd, pending_blocks[block.id]);
        }
        // Handle children of this block
        for (auto chld: fwd) handle_block(chld);
        return should_forward;
    }

    /**
     * Handles a received transaction. Returns true if the transaction
     * was new, false if it was already there.
     */
    virtual bool handle_transaction(const TinyTransaction& tx) {
        std::lock_guard<std::mutex> transaction_lock(transaction_mutex);
        if (satisfies<TinyTransaction>(received_transactions, tx.id, [](const TinyTransaction& tx) {
            return tx.id != (std::size_t) -1;
        })) return false;
        vec_set(received_transactions, tx.id, tx);
        return true;
    }

    /**
     * Send a message to all neighbours.
     */
    virtual void forward(Message<TinyData> msg) {
        manager().iter_neighbours(id(), [&msg, this] (node_id_t neigh) {
            manager().send_message(id(), neigh, msg);
            return true;
        });
    }

    /**
     *  Forwards all received messages to its neighbours and calls the handlers
     *  for the two kinds of messages.
     */
    void handle_message(Message<TinyData> msg) override {
        if (std::holds_alternative<TinyTransaction>(msg.data())) { // We received a transaction
            if (!handle_transaction(std::get<TinyTransaction>(msg.data()))) return;
        } else { // We received a block
            if (!handle_block(std::get<TinyBlock>(msg.data()))) return;
        }
        forward(msg);
    }

    /**
     * Generates a new transaction.
     */
    virtual void start_message(Message<TinyData> msg) override {
        TinyTransaction tx{id(), rng(), fmod(((double)rng()/1000000), balance)*0.99};
        while (tx.destination_node == id()) tx.destination_node = rng();
        balance -= tx.amount;
        handle_transaction(tx);
        set_data(msg, tx);
        forward(msg);
    };
public:
    static double block_reward;
    static double transaction_reward;
    /**
     * Returns a view of the current blockchain.
     */
    auto get_blockchain() const {
        return std::pair<const decltype(blockchain)&, decltype(head)>(blockchain, head);
    }

    /**
     * Constructor.
     */
    TinyNode(HardwareManager<TinyData>* manager, node_id_t id):
        Node(manager, id), balance(rng() % 1024 + 16) {}
    virtual ~TinyNode() = default;
};

/**
 * Interface that decides the behaviour of a miner.
 * It defaults to the behaviour of an honest miner.
 */
class MinerPolicy {
protected:
    const std::vector<TinyTransaction>& transactions;
    const std::set<std::size_t>& pending_transactions;
    const std::vector<TinyBlock>& blockchain;
    const std::vector<std::size_t>& lengths;
    const std::size_t& head;
    const std::size_t id;
    const std::function<void(TinyBlock)> send_block;
    std::mutex& pending_lock;
public:
    static std::size_t transactions_per_block;
    virtual void on_mined() {
        TinyBlock blk{head, id};
        {
            std::lock_guard<std::mutex> lck(pending_lock);
            for (auto tx: pending_transactions) {
                if (blk.transactions->size() > transactions_per_block) break;
                blk.transactions->push_back(transactions[tx]);
            }
        }
        send_block(blk);
    }
    virtual void on_block(const TinyBlock& block) {}
    virtual void on_transaction(const TinyTransaction& tx) {}
    MinerPolicy(
        const std::vector<TinyTransaction>& transactions,
        const std::set<std::size_t>& pending_transactions,
        const std::vector<TinyBlock>& blockchain,
        const std::vector<std::size_t>& lengths,
        const std::size_t& head,
        const std::size_t id,
        const std::function<void(TinyBlock)> send_block,
        std::mutex& pending_lock
    ):
        transactions(transactions), pending_transactions(pending_transactions),
        blockchain(blockchain), lengths(lengths), head(head), id(id), send_block(send_block),
        pending_lock(pending_lock) {}
    virtual ~MinerPolicy() = default;
};

template<typename Policy, typename... Args>
std::unique_ptr<MinerPolicy> make_policy(
    const std::vector<TinyTransaction>& transactions,
    const std::set<std::size_t>& pending_transactions,
    const std::vector<TinyBlock>& blockchain,
    const std::vector<std::size_t>& lengths,
    const std::size_t& head,
    const std::size_t id,
    const std::function<void(TinyBlock)> send_block,
    std::mutex& pending_lock,
    Args... args) {
    return std::make_unique<Policy>(
        transactions, pending_transactions, blockchain, lengths, head, id, send_block, pending_lock, args...
    );
}

class TinyMiner: public TinyNode {
protected:
    std::set<std::size_t> pending_transactions;
    std::unique_ptr<MinerPolicy> policy;
    std::size_t power_;
    std::mutex pending_lock;

    /**
     * Gets called whenever we confirm a block.
     */
    void confirm(const TinyBlock& blk) override {
        balance += block_value(blk);
        std::lock_guard<std::mutex> lck(pending_lock);
        for (auto tx: *blk.transactions)
            pending_transactions.erase(tx.id);
    }

    /**
     * Gets called whenever we unconfirm a block.
     */
    void unconfirm(const TinyBlock& blk) override {
        balance -= block_value(blk);
        std::lock_guard<std::mutex> lck(pending_lock);
        for (auto tx: *blk.transactions)
            pending_transactions.insert(tx.id);
    }

    /**
     * Handles a received transaction. Returns true if the transaction
     * was new, false if it was already there.
     */
    bool handle_transaction(const TinyTransaction& tx) override {
        if (TinyNode::handle_transaction(tx) == false) return false;
        {
            std::lock_guard<std::mutex> lck(pending_lock);
            pending_transactions.insert(tx.id);
        }
        if (policy) policy->on_transaction(tx);
        return true;
    }

    /**
     * Handles a received block. Returns true if the block
     * was new, false if it was already there.
     */
    bool handle_block(const TinyBlock& block) override {
        if (TinyNode::handle_block(block) == false) return false;
        if (policy) policy->on_block(block);
        return true;
    }

    /**
     *  Sends a block to all neighbours.
     */
    void send_block(const TinyBlock& blk) {
        Message<TinyData> msg;
        handle_block(blk);
        set_data(msg, blk);
        forward(msg);
    }

    /**
     * Generates a new transaction or a new block.
     */
    void start_message(Message<TinyData> msg) override {
        if (std::holds_alternative<TinyTransaction>(msg.data())) { // We should generate a transaction
            TinyNode::start_message(msg);
        } else {
            if (policy) policy->on_mined();
        }
    };

public:
    /**
     * Constructor.
     */
    template<typename MPolicy, typename... Args>
    TinyMiner(HardwareManager<TinyData>* manager, node_id_t id, std::size_t power, MPolicy* dummy, const Args&... args):
        TinyNode(manager, id), policy(make_policy<MPolicy>(
            received_transactions, pending_transactions, blockchain, lengths, head,
            id, std::bind(&TinyMiner::send_block, this, std::placeholders::_1), pending_lock, args...
        )), power_(power) {}

    TinyMiner(): TinyNode(nullptr, -1), policy(nullptr), power_(0) {}
    virtual ~TinyMiner() = default;
    /**
     * Returns the mining power of the miner.
     */
    std::size_t power() const {
        return power_;
    }
};

#endif
