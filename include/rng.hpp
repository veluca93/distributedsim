#ifndef DISTSIM_RNG_HPP
#define DISTSIM_RNG_HPP
#include <stdint.h>
#include <vector>
#include <algorithm>
#include <iostream>
#include <thread>

class xoroshiro {
    // xoroshiro128plus, http://xoroshiro.di.unimi.it/    
    uint64_t s0, s1;
    static inline uint64_t rotl(const uint64_t x, int k) {
	    return (x << k) | (x >> (64 - k));
    }
public:
    typedef uint64_t result_type;
    static result_type max() {return std::numeric_limits<uint64_t>::max();}
    static result_type min() {return 1;}
    xoroshiro(uint64_t s0, uint64_t s1): s0(s0), s1(s1) {}
    xoroshiro(): xoroshiro(1, 0) {}
    /**
     * Returns a new random number
     */
    uint64_t operator()() {
	    const uint64_t result = s0 + s1;
	    s1 ^= s0;
	    s0 = rotl(s0, 55) ^ s1 ^ (s1 << 14); // a, b
	    s1 = rotl(s1, 36); // c
    	return result;
    }
    /**
     * Returns a new random number from lower up to upper (exclusive).
     */
    uint64_t operator()(uint64_t lower, uint64_t upper) {
    	return (*this)()%(upper-lower)+lower;
    }
    /**
     * Returns a new random number up to upper (exclusive).
     */
    uint64_t operator()(uint64_t upper) {
    	return (*this)(0, upper);
    }
    /**
     * Returns up to amount distinct numbers between lower and upper (exclusive), without any number from excluded.
     */
    std::vector<uint64_t> get_distinct(uint64_t amount, uint64_t lower, uint64_t upper, const std::vector<uint64_t>& excluded = {}) {
        std::vector<uint64_t> ans;
        if (amount + lower + excluded.size() >= upper) {
            unsigned pos = 0;
            for (uint64_t i=lower; i<upper; i++) {
                while (pos < excluded.size() && excluded[pos] <= i) pos++;
                if (pos < excluded.size() && excluded[pos] == i) continue;
                ans.push_back(i);
            }
        } else {
            for (uint64_t i=0; i<amount; i++) {
                ans.push_back((*this)(lower, upper-amount-excluded.size()));
            }
            std::sort(ans.begin(), ans.end());
            unsigned pos = 0;
            for (uint64_t i=0; i<ans.size(); i++) {
                while (pos < excluded.size() && excluded[pos] <= ans[i]+i+pos) pos++;
                ans[i] += i + pos;
            }
        }
        return ans;
    }
    /**
     * Returns up to amount distinct numbers up to upper (exclusive), without any number from excluded.
     */
    std::vector<uint64_t> get_distinct(uint64_t amount, uint64_t upper, const std::vector<uint64_t>& excluded = {}) {
        return get_distinct(amount, 0, upper, excluded);
    }

    /**
     * Choose an element with probability proportional to its weight.
     *
     * Takes as an argument the vector of the prefix sums of the weights.
     */
    std::uint64_t choose_weighted(const std::vector<std::uint64_t>& weight_ps) {
        std::uint64_t rand = (*this)(weight_ps.back()+1);
        return std::lower_bound(weight_ps.begin(), weight_ps.end(), rand) - weight_ps.begin();
    }
};

extern thread_local xoroshiro rng;

#endif
