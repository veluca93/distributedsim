#ifndef TINYCOIN_SELFISH_HPP
#define TINYCOIN_SELFISH_HPP
#include "tinycoin.hpp"

class SelfishPolicy;

class SelfishCoordinator: public TinyMiner {
    std::deque<TinyBlock> our_chain;
    std::map<node_id_t, SelfishPolicy*> members;
    std::set<std::size_t> blocks_seen;
    std::size_t starting_height = 0;
    std::size_t published_blocks = 0;
    std::size_t our_head = 0;
    std::mutex m;
    std::set<std::size_t> private_pending_transactions;
    void send_block(const TinyBlock& blk);
    void clear_chain() {
        our_chain.clear();
        published_blocks = 0;
        our_head = head;
        starting_height = lengths[head];
        private_pending_transactions = pending_transactions;
    }
    void add_block(const TinyBlock& blk) {
        for (auto tx: *blk.transactions) private_pending_transactions.erase(tx.id);
        our_chain.push_back(blk);
        our_head = blk.id;
    }
    void flush_chain_(std::vector<TinyBlock>& to_send) {
        if (members.size() == 0) return;
        for (auto blk: our_chain) to_send.push_back(blk);
        clear_chain();
    }
public:
    virtual void forward(Message<TinyData>) override {}
    void add_member(node_id_t id, SelfishPolicy* ptr) {
        members.emplace(id, ptr);
    }
    bool is_member(node_id_t id) {
        return members.count(id);
    }
    std::size_t get_head() {return our_head;}
    void transaction(const TinyTransaction& tx) {
        handle_transaction(tx);
        std::lock_guard<std::mutex> lck(m);
        private_pending_transactions.insert(tx.id);
    }

    void add_transactions(TinyBlock& blk) {
        std::lock_guard<std::mutex> lck(m);
        for (auto tx: private_pending_transactions) {
            // Skip unknown transactions
            if (blk.transactions->size() > MinerPolicy::transactions_per_block) break;
            blk.transactions->push_back(received_transactions[tx]);
        }
    }

    void others_block(const TinyBlock& blk) {
        handle_block(blk);
        std::vector<TinyBlock> to_send;
        {
            std::lock_guard<std::mutex> lck(m);
            if (is_member(blk.miner)) return;
            if (blocks_seen.count(blk.id)) return;
            blocks_seen.insert(blk.id);
            // If the block is not the new head, ignore it.
            if (blk.id != head) return;
            if (starting_height + our_chain.size() + published_blocks < lengths[blk.id]) {
                // If the others have more blocks, give up and clear our branch
                clear_chain();
            } else if (starting_height + our_chain.size() + published_blocks == lengths[blk.id]) {
                // If the branches are now tied, publish the private block and hope for the best.
                to_send.push_back(our_chain.front());
                our_chain.pop_front();
                published_blocks++;
            } else if (starting_height + our_chain.size() + published_blocks == lengths[blk.id] + 1) {
                // We have a lead of 1, do not waste it.
                flush_chain_(to_send);
            } else {
                // We have a lead of more than 1, publish our first block and keep waiting.
                to_send.push_back(our_chain.front());
                our_chain.pop_front();
                published_blocks++;
            }
        }
        for (auto blk: to_send) send_block(blk);
    }
    void our_block(const TinyBlock& blk) {
        std::vector<TinyBlock> to_send;
        {
            std::lock_guard<std::mutex> lck(m);
            add_block(blk);
            // If the chains were tied and our branch was at least 1 long
            if (
                (starting_height + our_chain.size() + published_blocks == lengths[head] + 1)
                && published_blocks + our_chain.size() > 1
            ) { // We won the "tie race": send all the blocks left and clear our branch.
                flush_chain_(to_send);
            }
        }
        for (auto blk: to_send) send_block(blk);
    }

    void flush_chain() {
        std::vector<TinyBlock> to_send;
        {
            std::lock_guard<std::mutex> lck(m);
            flush_chain_(to_send);
        }
        for (auto blk: to_send) send_block(blk);
    }
};

class SelfishPolicy: public MinerPolicy {
    friend class SelfishCoordinator;
    SelfishCoordinator& coord;
public:
    virtual void on_mined() override {
        TinyBlock blk{coord.get_head(), id};
        coord.add_transactions(blk);
        coord.our_block(blk);
    }

    virtual void on_block(const TinyBlock& blk) override {
        coord.others_block(blk);
    }

    virtual void on_transaction(const TinyTransaction& tx) override {
        coord.transaction(tx);
    }

    SelfishPolicy(
        const std::vector<TinyTransaction>& transactions,
        const std::set<std::size_t>& private_pending_transactions,
        const std::vector<TinyBlock>& blockchain,
        const std::vector<std::size_t>& lengths,
        const std::size_t& head,
        const std::size_t id,
        const std::function<void(TinyBlock)> send_block,
        std::mutex& pending_lock,
        SelfishCoordinator* coord
    ):
        MinerPolicy(transactions, private_pending_transactions, blockchain, lengths, head, id, send_block, pending_lock),
        coord(*coord) {
            coord->add_member(id, this);
    }
};


void SelfishCoordinator::send_block(const TinyBlock& blk) {
    handle_block(blk);
    for (auto [_, ptr]: members) {
        [[maybe_unused]] auto tmp = _; // Shut up the compiler's complaints
        ptr->send_block(blk);
    }
}
#endif
