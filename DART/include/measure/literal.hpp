#pragma once

#include <chrono>

#include "measure/shortname.hpp"

// metric prefix

constexpr uint64_t operator"" _G(const unsigned long long x) noexcept {
    return 1'000'000'000ull * x;
}
constexpr uint64_t operator"" _M(const unsigned long long x) noexcept {
    return 1'000'000ull * x;
}
constexpr uint64_t operator"" _k(const unsigned long long x) noexcept {
    return 1'000ull * x;
}
constexpr uint64_t operator"" _(const unsigned long long x) noexcept {
    return 1ull * x;
}

// time

using namespace std::literals::chrono_literals;
constexpr auto us_per_s = 1_M;
constexpr auto s_to_us = double(1_M);
constexpr auto us_to_s = double(1) / 1_M;
constexpr auto ns_to_us = double(1) / 1_k;
constexpr auto ns_to_s = double(1) / 1_G;

// bin prefix

constexpr uint64_t operator"" _GiB(const unsigned long long x) noexcept {
    return 1024ull * 1024ull * 1024ull * x;
}
constexpr uint64_t operator"" _MiB(const unsigned long long x) noexcept {
    return 1024ull * 1024ull * x;
}
constexpr uint64_t operator"" _KiB(const unsigned long long x) noexcept {
    return 1024ull * x;
}
constexpr uint64_t operator"" _B(const unsigned long long x) noexcept {
    return x;
}

// usage: 3 (GiB) == 3 * GiB_to_MiB (MiB)
#define Byte2Byte(a, b) constexpr auto a ## _to_ ## b = double(1_ ## a) / 1_ ## b;
Byte2Byte(GiB, MiB); Byte2Byte(GiB, KiB); Byte2Byte(GiB, B);
Byte2Byte(MiB, GiB); Byte2Byte(MiB, KiB); Byte2Byte(MiB, B);
Byte2Byte(KiB, GiB); Byte2Byte(KiB, MiB); Byte2Byte(KiB, B);
Byte2Byte(B, GiB); Byte2Byte(B, MiB); Byte2Byte(B, KiB);

// usage: 3 (GiBps) == 3 * GiBps_to_MiBps (MiBps)
#define Byte2Byte_ps(a, b) constexpr auto a ## ps_to_ ## b ## ps = double(1_ ## a) / 1_ ## b;
Byte2Byte_ps(GiB, MiB); Byte2Byte_ps(GiB, KiB); Byte2Byte_ps(GiB, B);
Byte2Byte_ps(MiB, GiB); Byte2Byte_ps(MiB, KiB); Byte2Byte_ps(MiB, B);
Byte2Byte_ps(KiB, GiB); Byte2Byte_ps(KiB, MiB); Byte2Byte_ps(KiB, B);
Byte2Byte_ps(B, GiB); Byte2Byte_ps(B, MiB); Byte2Byte_ps(B, KiB);

// usage: 3 (GiBps) == 3 * GiBps_to_Gbps (Gbps)
#define Byte2bit_ps(a, b) constexpr auto a ## ps_to_ ## b ## bps = double(1_ ## a) / 1_ ## b * 8
Byte2bit_ps(GiB, G); Byte2bit_ps(GiB, M); Byte2bit_ps(GiB, k); Byte2bit_ps(GiB,);
Byte2bit_ps(MiB, G); Byte2bit_ps(MiB, M); Byte2bit_ps(MiB, k); Byte2bit_ps(MiB,);
Byte2bit_ps(KiB, G); Byte2bit_ps(KiB, M); Byte2bit_ps(KiB, k); Byte2bit_ps(KiB,);
Byte2bit_ps(B, G); Byte2bit_ps(B, M); Byte2bit_ps(B, k); Byte2bit_ps(B,);

[[nodiscard]] inline std::string readable_byte(uint64_t byte) noexcept {
    if (byte == 0) return "0";
    std::string result = "";
    uint64_t B = byte / 1_B % 1024;
    if (B != 0) result = std::to_string(B) + " B" + (result == "" ? "" : ", ") + result;
    uint64_t KiB = byte / 1_KiB % 1024;
    if (KiB != 0) result = std::to_string(KiB) + " KiB" + (result == "" ? "" : ", ")  + result;
    uint64_t MiB = byte / 1_MiB % 1024;
    if (MiB != 0) result = std::to_string(MiB) + " MiB" + (result == "" ? "" : ", ")  + result;
    uint64_t GiB = byte / 1_GiB;
    if (GiB != 0) result = std::to_string(GiB) + " GiB" + (result == "" ? "" : ", ")  + result;
    return result;
}

// pointer

[[nodiscard]] constexpr uintptr_t signed48to64(uintptr_t p) noexcept {
    // to signed, then unsigned
    return (int64_t)p << 16 >> 16;
}

constexpr uint64_t pointer_48bit_mask = 0x0000'ffff'ffff'ffffull;

[[nodiscard]] constexpr uint64_t ALIGN(uint64_t ptr, uint64_t data) noexcept {
    return ((ptr + (data - 1)) / data * data);
}

[[nodiscard]] constexpr uint64_t ALIGN_Byte(uint64_t ptr, uint64_t byte) noexcept {
    return ALIGN(ptr, byte * 1_B);
}

[[nodiscard]] constexpr uint64_t ALIGN_KiB(uint64_t ptr, uint64_t KiB = 1) noexcept {
    return ALIGN(ptr, KiB * 1_KiB);
}

[[nodiscard]] constexpr uint64_t ALIGN_MiB(uint64_t ptr, uint64_t MiB = 1) noexcept {
    return ALIGN(ptr, MiB * 1_MiB);
}

[[nodiscard]] constexpr uint64_t ALIGN_GiB(uint64_t ptr, uint64_t GiB = 1) noexcept {
    return ALIGN(ptr, GiB * 1_GiB);
}

[[nodiscard]] inline std::string hex_str(uintptr_t ptr) noexcept {
    std::stringstream s;
    s << std::hex << ptr << std::dec;
    return "0x" + s.str();
}

[[nodiscard]] inline std::string hex_str_no_0x(uintptr_t ptr) noexcept {
    std::stringstream s;
    s << std::hex << ptr << std::dec;
    return s.str();
}