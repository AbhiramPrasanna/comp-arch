#pragma once
#include <iostream>

#include <span>
#include <fstream>
#include <cstdint>
#include <type_traits>

#include "measure/shortname.hpp"
#include "measure/literal.hpp"

// KeyType better use these below
//   key_type:: str | u64 | soft
//   or just use str, u64
//   or even use bool/int/double, but they are not implemented in ycsb.h or art.h

namespace key {

using save_t = struct {
    u64 offset;
    u64 size;
};

constexpr u64 typical_leaf_len = 16;

// pos must be [0, key_len - 1]
constexpr u8 get_byte(span key, u64 pos) noexcept {
    if (pos >= key.size())
        return 0;
    return key[pos];
}

// prefix_len must be [0, key_len - 1]
constexpr span get_prefix(span key, u64 prefix_len) noexcept {
    return span(key.begin(), key.begin() + prefix_len);
}

// pos must be [0, key_len - 1]
constexpr bool compare_byte(span key1, span key2, u64 pos) noexcept {
    return key1[pos] == key2[pos];
}

constexpr u64 get_first_not_same_pos(span key1, span key2, u64 start_place) noexcept {
    u64 now_place = start_place;
    while (true) {
        if (now_place >= key1.size() || now_place >= key2.size())
            return now_place;
        if (!compare_byte(key1, key2, now_place))
            return now_place;
        now_place++;
    }
}

inline u64 span_to_u64(span data) noexcept {
    u64 s;
    for (u64 i = 0; i < sizeof(u64); ++i) {
        reinterpret_cast<u8*>(&s)[i] = data[i];
    }
    return s;
}

inline u64 span_to_u64_reverse(span data) noexcept {
    u64 s;
    for (u64 i = 0; i < sizeof(u64); ++i) {
        reinterpret_cast<u8*>(&s)[i] = data[sizeof(u64) - i - 1];
    }
    return s;
}


inline viw span_to_view(span data) noexcept {
    return viw(reinterpret_cast<const char*>(data.data()), data.size());
}

inline str to_string_only(span value) noexcept {
    str res;
    for (auto c : value)
        res += c;
    return res;
}

inline str to_string_only_no_0(span value) noexcept {
    str res;
    for (auto c : value) {
        if (c == 0) break;
        res += c;
    }
    return res;
}

inline str to_string(span key) noexcept {
    str res = "<length> " + std::to_string(key.size()) + ", <hex> ";

    for (auto c : key)
        res += "[" + std::to_string(c) + " " + hex_str(c) + "] ";
    res.pop_back();

    // small-endian
    res += ", <u64(just-read)> " + std::to_string(span_to_u64(key));
    res += ", <u64(reverse)> " + std::to_string(span_to_u64_reverse(key));

    res += ", <str> ";
    for (auto c : key)
        res += c;

    return res;
}

}

namespace key_value_buffer {

class BufferBlock {
public:
    BufferBlock() = default;
    ~BufferBlock() = default;
    void read_from_file(const str& file_name) {
        std::ifstream read_file(file_name, std::ios::binary);
        if (read_file.is_open()) {
            u64 size = 0;
            read_file.read(reinterpret_cast<char*>(&size), sizeof(size));
            big_buffer.resize(size);
            read_file.read(reinterpret_cast<char*>(big_buffer.data()), big_buffer.size() * sizeof(u8));
            read_file.close();
            data_valid = true;
        } else {
            data_valid = false;
        }
    }
    void write_to_file(const str& file_name) {
        std::ofstream write_file(file_name, std::ios::binary);
        if (write_file.is_open()) {
            u64 size = big_buffer.size();
            write_file.write(reinterpret_cast<char*>(&size), sizeof(size));
            write_file.write(reinterpret_cast<char*>(big_buffer.data()), big_buffer.size() * sizeof(u8));
            write_file.close();
            data_valid = true;
        } else {
            data_valid = false;
        }
    }
    u8& operator[](u64 pos) noexcept {
        return big_buffer[pos];
    }
    void add_span(span data) noexcept {
        for (auto c : data)
            big_buffer.push_back(c);
    }
    void add_u64(u64 data) noexcept {
        u8* data_ptr = reinterpret_cast<u8*>(&data);
        for (u64 i = sizeof(u64) - 1; i < sizeof(u64); --i)
            big_buffer.push_back(data_ptr[i]);
    }
    void add_strviw(const viw& data) noexcept {
        for (auto c : data)
            big_buffer.push_back(c);
    }
    void add_strviw_more_0(const viw& data) noexcept {
        for (auto c : data)
            big_buffer.push_back(c);
        big_buffer.push_back(0);
    }
    span get_span(key::save_t data) noexcept {
        return span(big_buffer.begin() + data.offset, data.size);
    }
    span get_span(u64 offset, u64 size) noexcept {
        return span(big_buffer.begin() + offset, size);
    }
    u64 get_u64(key::save_t data) noexcept {
        return key::span_to_u64(get_span(data));
    }
    u64 get_u64(u64 offset, u64 size) noexcept {
        return key::span_to_u64(get_span(offset, size));
    }
    viw get_strview(key::save_t data) noexcept {
        return key::span_to_view(get_span(data));
    }
    viw get_strview(u64 offset, u64 size) noexcept {
        return key::span_to_view(get_span(offset, size));
    }
    bool is_data_valid() const noexcept {
        return data_valid;
    }
    uint32_t size() const noexcept {
        return big_buffer.size();
    }
private:
    vec<u8> big_buffer;
    bool data_valid = false;
};

}