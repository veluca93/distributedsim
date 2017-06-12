#ifndef DISTSIM_COMMON_HPP
#define DISTSIM_COMMON_HPP
#include <cstddef>
#include <vector>
#include <functional>

template <typename T>
class Node;

template <typename T>
class Message;

template <typename T>
class HardwareManager;

typedef std::size_t node_id_t;

template<typename T>
bool satisfies(const std::vector<T>& vec, std::size_t pos, const std::function<bool(const T& v)>& f) {
    return vec.size() > pos && f(vec[pos]);
}

template<typename T>
void vec_set(std::vector<T>& vec, std::size_t pos, const T& v) {
    if (vec.size() <= pos) vec.resize(pos+1);
    vec[pos] = v;
}

#endif
