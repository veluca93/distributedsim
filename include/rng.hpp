#ifndef DISTSIM_RNG_HPP
#define DISTSIM_RNG_HPP
#include <stdint.h>

class xoroshiro {
    // xoroshiro128plus, http://xoroshiro.di.unimi.it/    
    uint64_t s[2];
    static inline uint64_t rotl(const uint64_t x, int k) {
	    return (x << k) | (x >> (64 - k));
    }
public:
    xoroshiro(uint64_t s0, uint64_t s1) {s[0] = s0; s[1] = s1;}
    xoroshiro(): xoroshiro(1, 0) {}
    uint64_t operator()() {
	    const uint64_t s0 = s[0];
	    uint64_t s1 = s[1];
	    const uint64_t result = s0 + s1;
	    s1 ^= s0;
	    s[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14); // a, b
	    s[1] = rotl(s1, 36); // c
    	return result;
    }
};
#endif
