#include "config.hpp"
#include "hashpower.hpp"
#include "tinycoin.hpp"
#include "graph_gen.hpp"
#include "graph_hwm.hpp"
#include "miner_chooser.hpp"
#include "selfish.hpp"
#include "mem_wrap.hpp"
#include <iostream>
#include <chrono>

using namespace std::literals;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " config_file" << std::endl;
        return -1;
    }
    Config cfg{argv[1]};
    std::function<long long(std::string)> stoll = [](std::string s) {return std::stoll(s);};
    std::function<double(std::string)> stod = [](std::string s) {return std::stod(s);};
    std::function<std::string(std::string)> stos = [](std::string s) {return s;};
    TinyBlock::delay_per_transaction = std::chrono::nanoseconds(cfg.get("delay_per_transaction", 20LL, stoll));
    TinyTransaction::delay = std::chrono::nanoseconds(cfg.get("delay_per_transaction", 20LL, stoll));
    TinyBlock::base_delay = std::chrono::nanoseconds(cfg.get("base_delay", 100LL, stoll));
    TinyNode::block_reward = cfg.get("block_reward", 1.0, stod);
    TinyNode::transaction_reward = cfg.get("transaction_reward", 0.01, stod);
    MinerPolicy::transactions_per_block = cfg.get("transactions_per_block", 50LL, stoll);
    std::string network_kind = cfg.get("network_kind", "erdos"s, stos);
    long long network_size = cfg.get("network_size", 20LL, stoll);
    long long network_connectivity = cfg.get("network_connectivity", 100LL, stoll);
    long long S = cfg.get("seed", 0LL, stoll);
    rng = xoroshiro(-1, S);
    long long nthreads = cfg.get("nthreads", -1LL, stoll);
    if (nthreads == -1) nthreads = std::thread::hardware_concurrency();
    edge_list_t edges;
    if (network_kind == "erdos"s) edges = gen_conn_erdos(network_size, network_connectivity, S);
    else if (network_kind == "barabasi"s) edges = gen_barabasi_albert(network_size, network_connectivity, S);
    else {
        std::cerr << "Unknown graph type " << network_kind << "! Valid types are: erdos, barabasi" << std::endl;
        return -1;
    }
    long long num_miners = network_size * cfg.get("miners_percent", 0.2, stod);
    double selfish_percent = cfg.get("selfish_percent", 0.0, stod);
    double selfish_power_percent = cfg.get("selfish_power_percent", selfish_percent, stod);
    long long num_selfish = num_miners * selfish_percent;
    long long num_honest = num_miners - num_selfish;
    std::string selfish_algo = cfg.get("selfish_algo", "random"s, stos);
    auto [honest, selfish] = choose_miners(network_size, num_honest, num_selfish, edges, selfish_algo, S); 
    auto [honest_powers, selfish_powers] = get_hashpower(num_honest, num_selfish, selfish_power_percent, S);
    uint64_t honest_total_power = 0;
    uint64_t selfish_total_power = 0;
    for (auto x: honest_powers) honest_total_power += x;
    for (auto x: selfish_powers) selfish_total_power += x;
    std::cout.precision(2);
    std::cout.setf(std::ios::fixed, std::ios::floatfield);
    std::cout << "There are " << network_size << " nodes and " << edges.size() << " edges." << std::endl;
    std::cout << num_honest << " honest miners have " << honest_total_power << " mining power." << std::endl;
    std::cout << num_selfish << " selfish miners have " << selfish_total_power << " mining power." << std::endl;
    std::cout << (100.0*num_selfish/(num_honest+num_selfish)) << "% of the miners are selfish." << std::endl;
    std::cout << "They control " << (100.0*selfish_total_power/(selfish_total_power+honest_total_power)) <<
        "% of the total mining power." << std::endl;

    SelfishCoordinator coord;
    GraphHardwareManager<TinyData> hwm(nthreads, S);
    std::vector<uint64_t> miner_weights_ps;
    for (long long i=0; i<network_size; i++) {
        if (honest.count(i)) {
            auto pwr = honest_powers.back();
            honest_powers.pop_back();
            hwm.add_node<TinyMiner>(pwr, (MinerPolicy*) NULL);
            miner_weights_ps.push_back(pwr);
        } else if (selfish.count(i)) {
            auto pwr = selfish_powers.back();
            selfish_powers.pop_back();
            hwm.add_node<TinyMiner>(pwr, (SelfishPolicy*) NULL, &coord);
            miner_weights_ps.push_back(pwr);
        } else {
            hwm.add_node<TinyNode>();
            miner_weights_ps.push_back(0);
        }
    }
    for (unsigned i=1; i<miner_weights_ps.size(); i++) {
        miner_weights_ps[i] += miner_weights_ps[i-1];
    }
    for (auto edg: edges) hwm.add_edge(edg.first, edg.second);
    hwm.run();

    auto transaction_interval = std::chrono::microseconds(cfg.get("transaction_interval", 1000LL, stoll));
    auto block_interval = std::chrono::microseconds(cfg.get("block_interval", 10000LL, stoll));
    auto final_wait = std::chrono::microseconds(cfg.get("final_wait", 10000LL, stoll));
    const auto block_num = cfg.get("block_num", 1000LL, stoll);
    auto last_block = std::chrono::high_resolution_clock::now();
    std::atomic<long long> tx_done = 0;
    std::atomic<long long> blocks_done = 0;
    std::thread status_thread([&]() {
        while (Node<TinyData>::queued_messages || blocks_done < block_num) {
            printf(
                "% 9lld/% 9lld blocks, %12lld transactions, % 12lld/% 12lld events left\r",
                (long long)blocks_done, block_num, (long long)tx_done,
                (long long)Node<TinyData>::queued_messages, (long long)Node<TinyData>::all_messages
            );
            fflush(stdout);
            std::this_thread::sleep_for(100ms);
        }
        printf(
            "% 9lld/% 9lld blocks, %12lld transactions, % 12lld/% 12lld events left\n",
            (long long)blocks_done, block_num, (long long)tx_done,
            (long long)Node<TinyData>::queued_messages, (long long)Node<TinyData>::all_messages
        );
    });
    for (; blocks_done < block_num;) {
        auto now = std::chrono::high_resolution_clock::now();
        if (now > last_block + block_interval) {
            int miner = rng.choose_weighted(miner_weights_ps);
            hwm.gen_message(miner, TinyData{TinyBlock()});
            last_block = now;
            blocks_done++;
        }
        int tx_origin = hwm.get_random_node();
        hwm.gen_message(tx_origin, TinyData{TinyTransaction()});
        std::this_thread::sleep_for(transaction_interval);
        tx_done++;
    }
    coord.flush_chain();
    status_thread.join();
    hwm.stop();

    auto [blockchain, head] = ((TinyNode*)hwm.get(0))->get_blockchain();
    std::vector<std::size_t> split_num(blockchain.size(), 0);
    std::vector<std::size_t> split_len(blockchain.size(), 0);
    std::vector<bool> main_chain(blockchain.size(), false);
    for (; head != 0; head = blockchain[head].parent) {
        main_chain[head] = true;
    }
    std::size_t honest_blocks = 0;
    std::size_t selfish_blocks = 0;
    std::size_t total_splits = 0;
    std::size_t max_split_len = 0;
    for (auto blk: blockchain) {
        if (blk.id == 0) continue;
        if (blk.id == (std::size_t)-1) continue;
        if (honest.count(blk.miner) && main_chain[blk.id]) honest_blocks++;
        else if (selfish.count(blk.miner) && main_chain[blk.id]) selfish_blocks++;
        if (
            (split_num[blk.parent] && !main_chain[blk.id]) ||
            (main_chain[blk.parent] && !main_chain[blk.id])
        ) {
            split_len[blk.id] = 1;
            total_splits++;
            if (max_split_len < split_len[blk.id])
                max_split_len = split_len[blk.id];
        }
        split_num[blk.parent]++;
        if (split_len[blk.parent]) {
            split_len[blk.id] = split_len[blk.parent] + 1;
            if (max_split_len < split_len[blk.id])
                max_split_len = split_len[blk.id];
        }
    }
    std::cout << "There were " << total_splits << " blockchain splits." << std::endl;
    std::cout << "The longest split lasted for " << max_split_len << " blocks." << std::endl;
    std::cout << "Honest miners have mined " << honest_blocks << " real blocks." << std::endl;
    std::cout << "Selfish miners have mined " << selfish_blocks << " real blocks." << std::endl;
    std::cout << (100.0*selfish_blocks/(selfish_blocks+honest_blocks)) << "% of real blocks were mined by selfish miners" << std::endl;
}
