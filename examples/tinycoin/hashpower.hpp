#ifndef TINYCOIN_HASHPOWER_HPP
#define TINYCOIN_HASHPOWER_HPP
#include "rng.hpp"
#include <set>
#include <algorithm>

auto get_hashpower(uint64_t num_honest, uint64_t num_selfish, double selfish_percent, long long seed) {
    // Prefix sum of the chances of every kind of compute device:
    // CPU, GPU, FPGA, ASIC, ASIC_2, ASIC_3 (where the last two
    // are more powerful kinds of ASIC)
    static const std::vector<std::uint64_t> weight_ps{
        0b0100000,
        0b1000000,
        0b1001000,
        0b1001100,
        0b1001110,
        0b1001111
    };
    auto get_one = [&] () {
        long long pw = rng.choose_weighted(weight_ps);
        long long multiplier = 1;
        for (int i=0; i<pw; i++) multiplier *= 10;
        return rng(1, 10)*multiplier;
    };
    std::multiset<uint64_t> selfish_weights;
    std::multiset<uint64_t> honest_weights;
    uint64_t selfish_total = 0;
    uint64_t honest_total = 0;
    for (unsigned i=0; i<num_selfish; i++) {
        auto r = get_one();
        selfish_weights.insert(r);
        selfish_total += r;
    }
    for (unsigned i=0; i<num_honest; i++) {
        auto r = get_one();
        honest_weights.insert(r);
        honest_total += r;
    }
    if (selfish_percent != 0.0 && num_selfish == 0) throw std::runtime_error("Invalid selfish_percent!");
    while (true) {
        if (selfish_total < (selfish_total+honest_total)*(selfish_percent-0.01)) {
            if (rng(2)) {
                selfish_total -= *selfish_weights.begin();
                selfish_weights.erase(selfish_weights.begin());
                auto r = get_one();
                selfish_weights.insert(r);
                selfish_total += r;
            } else {
                honest_total -= *honest_weights.rbegin();
                honest_weights.erase(honest_weights.find(*honest_weights.rbegin()));
                auto r = get_one();
                honest_weights.insert(r);
                honest_total += r;
            }
        } else if (selfish_total > (selfish_total+honest_total)*(selfish_percent+0.01)) {
            if (rng(2)) {
                selfish_total -= *selfish_weights.rbegin();
                selfish_weights.erase(selfish_weights.find(*selfish_weights.rbegin()));
                auto r = get_one();
                selfish_weights.insert(r);
                selfish_total += r;
            } else {
                honest_total -= *honest_weights.begin();
                honest_weights.erase(honest_weights.begin());
                auto r = get_one();
                honest_weights.insert(r);
                honest_total += r;
            }
        } else break;
    }

    std::vector<uint64_t> s_ans{selfish_weights.begin(), selfish_weights.end()};
    std::vector<uint64_t> h_ans{honest_weights.begin(), honest_weights.end()};
    std::shuffle(s_ans.begin(), s_ans.end(), rng);
    std::shuffle(h_ans.begin(), h_ans.end(), rng);
    return std::make_pair(h_ans, s_ans);
}

#endif
