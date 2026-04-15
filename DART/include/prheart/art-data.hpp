#pragma once

#include "measure/measure.hpp"
#include "log/log.hpp"

#include "prheart/art-head.hpp"


namespace prheart {

enum class inhabited_result {
    find_none, cannot_find, find_different_key, find_same_key
};

// slot = 0x12'34'56'78'9a'bc'de'f0ull;
//          ^ [0..=2] type (3)
//          ^^ [3][0..=3] version (5)
//             ^^ [0..=3][0..=3] key_byte (8)
//                ^^ ^^ ^^ ^^ ^^ ^ last[0..=1] fptr (42), 48 - 6, 64bit aligned
//                               ^^ [2..=3][0..=3] length (6)

using slot_size_t = uint64_t;

enum class prheart_slot_data_offset : uint8_t {
    node_type = 0, node_type_end = 2,
    version = 3, version_end = 7,
    key_byte = 8, key_byte_end = 15,
    fptr = 16, fptr_end = 57,
    length = 58, length_end = 63
};

constexpr inline uint64_t mask_gen(uint8_t len, uint8_t start_pos) noexcept {
    uint64_t res = 1;
    for (uint32_t i = 0; i < len; ++i) res *= 2;
    res--;
    return res << (64 - start_pos - len);
}

constexpr inline uint64_t offset_mask_gen(prheart_slot_data_offset start_pos, prheart_slot_data_offset end_pos) noexcept {
    return mask_gen(uint8_t(end_pos) - uint8_t(start_pos) + 1, uint8_t(start_pos));
}

constexpr uint64_t node_type_mask = offset_mask_gen(prheart_slot_data_offset::node_type, prheart_slot_data_offset::node_type_end);
constexpr uint32_t node_type_left_shift = 63 - uint32_t(prheart_slot_data_offset::node_type_end);
constexpr uint64_t version_mask = offset_mask_gen(prheart_slot_data_offset::version, prheart_slot_data_offset::version_end);
constexpr uint32_t version_left_shift = 63 - uint32_t(prheart_slot_data_offset::version_end);
constexpr uint64_t key_byte_mask = offset_mask_gen(prheart_slot_data_offset::key_byte, prheart_slot_data_offset::key_byte_end);
constexpr uint32_t key_byte_left_shift = 63 - uint32_t(prheart_slot_data_offset::key_byte_end);
constexpr uint64_t fptr_mask = offset_mask_gen(prheart_slot_data_offset::fptr, prheart_slot_data_offset::fptr_end);
constexpr uint32_t fptr_left_shift = 63 - uint32_t(prheart_slot_data_offset::fptr_end);
constexpr uint64_t length_mask = offset_mask_gen(prheart_slot_data_offset::length, prheart_slot_data_offset::length_end);
constexpr uint32_t length_left_shift = 63 - uint32_t(prheart_slot_data_offset::length_end);


class PrheartSlotData final {
  private:
    slot_size_t data;

  public:
    PrheartSlotData() = default;
    ~PrheartSlotData() = default;
  
    [[nodiscard]] slot_size_t get_data() const noexcept;
    [[nodiscard]] str get_readable_str() const noexcept;

    [[nodiscard]] PrheartNodeType node_type() const noexcept;
    [[nodiscard]] str node_type_str() const noexcept;
    [[nodiscard]] uint8_t version() const noexcept;
    [[nodiscard]] str version_str() const noexcept;
    [[nodiscard]] uint8_t key_byte() const noexcept;
    [[nodiscard]] str key_byte_str() const noexcept;
    [[nodiscard]] uintptr_t fptr() const noexcept;
    [[nodiscard]] str fptr_str() const noexcept;
    [[nodiscard]] uint64_t length() const noexcept;
    [[nodiscard]] str length_str() const noexcept;

    void set_data(uint64_t data) noexcept;
    void set_data(const PrheartSlotData& data) noexcept;
    void set_node_type(PrheartNodeType type) noexcept;
    void set_version(uint8_t version) noexcept;
    void set_key_byte(uint8_t byte) noexcept;
    void set_fptr(uintptr_t ptr) noexcept;
    void set_length(uint64_t length) noexcept;

    [[nodiscard]] bool cas_local_data(uint64_t old_data, uint64_t new_data) noexcept;
    [[nodiscard]] bool cas_local_data(const PrheartSlotData& old_data_from_slot, const PrheartSlotData& new_data_from_slot) noexcept;

    void clear() noexcept;
  
    void show() const noexcept;
    void show_raw() const noexcept;
};

// please malloc space == sizeof(PrheartLeafData) + key_len + value_len
class PrheartLeafData final {
  public:
    uint32_t key_len;
    uint32_t value_len;
  public:
    PrheartLeafData() = default;
    ~PrheartLeafData() = default;
    [[nodiscard]] uintptr_t key_start() const {
        return (uintptr_t)this + sizeof(PrheartLeafData);
    }
    [[nodiscard]] uintptr_t value_start() const {
        return (uintptr_t)this + sizeof(PrheartLeafData) + key_len;
    }
    void set_key(uintptr_t src, uint32_t len) {
        key_len = len;
        memcpy((void*)key_start(), (void*)src, key_len);
    }
    void set_value(uintptr_t src, uint32_t len) {
        value_len = len;
        memcpy((void*)value_start(), (void*)src, value_len);
    }
    str get_readable_str() const noexcept {
        // get key (hex_str) and value (str)
        char buf[128];
        memcpy(buf, (char*)key_start(), key_len);
        buf[key_len] = '\0';
        str res = "key = 0x";
        for (uint32_t i = 0; i < key_len; ++i) {
            res += hex_str_no_0x(buf[i] & 0xff);
        }
        res += ", value = ";
        memcpy(buf, (char*)value_start(), value_len);
        buf[value_len] = '\0';
        res += str(buf);
        return res;
    }
};

[[nodiscard]] constexpr uint32_t type_to_size(PrheartNodeType type) noexcept {
    constexpr uint32_t slot_size = sizeof(PrheartSlotData);
    switch (type) {
    case PrheartNodeType::None: return slot_size * 0;
    case PrheartNodeType::Leaf: return slot_size * 1;
    case PrheartNodeType::Node8: return slot_size * 8;
    case PrheartNodeType::Node16: return slot_size * 16;
    case PrheartNodeType::Node32: return slot_size * 32;
    case PrheartNodeType::Node64: return slot_size * 64;
    case PrheartNodeType::Node128: return slot_size * 128;
    case PrheartNodeType::Node256: return slot_size * 256;
    default: return 0;
    }
}

}