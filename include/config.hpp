#ifndef DISTSIM_CONFIG_HPP
#define DISTSIM_CONFIG_HPP
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <functional>

class Config {
    std::unordered_map<std::string, std::string> kv;
public:
    Config(std::string path) {
        std::ifstream f(path);
        if (!f.good()) throw std::runtime_error("Could not open config file!");
        std::string line;
        auto trim = [] (std::string str) {
            str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
            str.erase(str.begin(), str.begin()+str.find_first_not_of(" \t"));
            return std::string(str.c_str());
        };
        while (getline(f, line)) {
            line = line.substr(0, line.find('#'));
            line = trim(line);
            if (line.length() == 0) continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) {
                std::cerr << "Line (" << line << ") does not contain =!" << std::endl;
                return;
            }
            kv.emplace(trim(line.substr(0, eq)), trim(line.substr(eq+1)));
        }
    }
    template<typename T>
    T get(const std::string key, T dv, std::function<T(std::string)> conv) const {
        if (kv.count(key) == 0) return dv;
        return conv(kv.at(key));
    }
    const auto& get() const {
        return kv;
    }
};

#endif
