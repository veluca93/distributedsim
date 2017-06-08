#ifndef DISTSIM_MESSAGE_HPP
#define DISTSIM_MESSAGE_HPP
#include "common.hpp"
#include "hardware_manager.hpp"
#include "node.hpp"
#include <cstddef>
#include <chrono>

template <typename T> // T should be default constructible
class Message {
    friend class HardwareManager<T>;
    std::size_t hops;
    std::chrono::microseconds delay_;
    T data_;
    Message(): hops(0), delay_(0) {}
public:
    T& data() {return data_;}
    std::size_t get_hops() {return hops;}
    template<typename D>
    void delay(const D& new_delay) {
        delay_ = std::chrono::duration_cast<std::chrono::microseconds>(new_delay);
    }
    auto delay() const {
        return delay_;
    }
    bool operator<(const Message<T>& other) const {
        return this < &other;
    }
};

#endif
