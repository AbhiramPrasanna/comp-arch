#pragma once

// use int8_t and so on
#include <cstddef>
#include <cstdint>

// some stl
#include <functional>
#include <map>
#include <string>
#include <sstream>
#include <tuple>
#include <vector>

// ptrs
#include <memory>

#include "log/stdlog.hpp"

class print_map : public std::unordered_map<int, int> {
public:
    void print() {
        for (auto& a : *this) {
            log_green << a.first << ": " << a.second << std::endl;
        }
    }
};