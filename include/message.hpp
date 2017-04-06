#ifndef DISTSIM_MESSAGE_HPP
#define DISTSIM_MESSAGE_HPP
#include "hardware_manager.hpp"

template <typename T>
class HardwareManager;

template <typename T> // T should be default constructible
class Message {
    friend class HardwareManager<T>;
    std::size_t hops;
    T data_;
    Message(): hops(0) {}
public:
    T& data() {return data_;}
    std::size_t get_hops() {return hops;}
};

#endif
