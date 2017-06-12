#include "tinycoin.hpp"

std::chrono::nanoseconds TinyTransaction::delay;
std::chrono::nanoseconds TinyBlock::delay_per_transaction;
std::chrono::nanoseconds TinyBlock::base_delay;
double TinyNode::block_reward;
double TinyNode::transaction_reward;
std::size_t MinerPolicy::transactions_per_block;

template<>
std::atomic<long long> Node<TinyData>::queued_messages{0};
template<>
std::atomic<long long> Node<TinyData>::all_messages{0};
