#include "magic_enum/magic_enum.hpp"

#include "log/log.hpp"

#include "prheart/art-head.hpp"
#include "prheart/art-data.hpp"


namespace prheart {

slot_size_t PrheartSlotData::get_data() const noexcept {
    return this->data;
}

str PrheartSlotData::get_readable_str() const noexcept {
    return
        std::string()
        + "node_type = " + node_type_str()
        + ", version = " + version_str()
        + ", key_byte = " + key_byte_str()
        + ", fptr = " + fptr_str()
        + ", length = " + length_str();
}

PrheartNodeType PrheartSlotData::node_type() const noexcept {
    return PrheartNodeType((this->data & node_type_mask) >> node_type_left_shift);
}

str PrheartSlotData::node_type_str() const noexcept {
    return str(magic_enum::enum_name(node_type()));
}

uint8_t PrheartSlotData::version() const noexcept {
    return (this->data & version_mask) >> version_left_shift;
}

str PrheartSlotData::version_str() const noexcept {
    return std::to_string((uint32_t)version());
}

uint8_t PrheartSlotData::key_byte() const noexcept {
    return (this->data & key_byte_mask) >> key_byte_left_shift;
}

str PrheartSlotData::key_byte_str() const noexcept {
    // dec and hex
    return std::to_string((uint32_t)key_byte()) + " (" + hex_str((uint32_t)key_byte()) + ")";
}

uintptr_t PrheartSlotData::fptr() const noexcept {
    return signed48to64((this->data & fptr_mask) >> fptr_left_shift << 6);
}

str PrheartSlotData::fptr_str() const noexcept {
    return hex_str(fptr());
}

uint64_t PrheartSlotData::length() const noexcept {
    return (this->data & length_mask) >> length_left_shift;
}

str PrheartSlotData::length_str() const noexcept {
    return std::to_string(length());
}

void PrheartSlotData::set_data(uint64_t data) noexcept {
    this->data = data;
}

void PrheartSlotData::set_data(const PrheartSlotData& data) noexcept {
    this->data = data.get_data();
}

void PrheartSlotData::set_node_type(PrheartNodeType n) noexcept {
    this->data = ((uint64_t(n) << node_type_left_shift) & node_type_mask) | (this->data & ~node_type_mask);
}

void PrheartSlotData::set_version(uint8_t n) noexcept {
    this->data = ((uint64_t(n) << version_left_shift) & version_mask) | (this->data & ~version_mask);
}

void PrheartSlotData::set_key_byte(uint8_t n) noexcept {
    this->data = ((uint64_t(n) << key_byte_left_shift) & key_byte_mask) | (this->data & ~key_byte_mask);
}

void PrheartSlotData::set_fptr(uintptr_t n) noexcept {
    this->data = ((uint64_t(n) >> 6 << fptr_left_shift) & fptr_mask) | (this->data & ~fptr_mask);
}

void PrheartSlotData::set_length(uint64_t n) noexcept {
    this->data = ((uint64_t(n) << length_left_shift) & length_mask) | (this->data & ~length_mask);
}

bool PrheartSlotData::cas_local_data(uint64_t old_data, uint64_t new_data) noexcept {
    return __sync_bool_compare_and_swap(&this->data, old_data, new_data);
}

bool PrheartSlotData::cas_local_data(const PrheartSlotData& old_data_from_slot, const PrheartSlotData& new_data_from_slot) noexcept {
    return __sync_bool_compare_and_swap(&this->data, old_data_from_slot.get_data(), new_data_from_slot.get_data());
}

void PrheartSlotData::clear() noexcept {
    this->data = 0ull;
}

void PrheartSlotData::show() const noexcept {
    log_info << get_readable_str() << std::endl;
}

void PrheartSlotData::show_raw() const noexcept {
    log_info << hex_str(get_data()) << std::endl;
}

}