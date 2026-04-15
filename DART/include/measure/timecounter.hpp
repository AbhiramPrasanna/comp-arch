#pragma once

#include <chrono>
#include <cstdio>
#include <sstream>
#include <ranges>
#include <utility>

#include "measure/shortname.hpp"
#include "measure/literal.hpp"

namespace counter {

class TimeCounter {

  protected:

    std::chrono::nanoseconds time_cost;
    uint64_t event_count;
    uint64_t rtt_count;
    uint64_t band_count;
    std::chrono::time_point<std::chrono::steady_clock> start_point;
    int thread_num = 1;

  public:

    TimeCounter() noexcept;
    ~TimeCounter() noexcept = default;

    void reset_time_counter() noexcept;

    void start() noexcept;
    void stop() noexcept;

    void set_thread_num(const int thread_num) noexcept;
    void set_all_time_cost(const std::chrono::nanoseconds& time = 0ns) noexcept;
    void add_all_time_cost(const std::chrono::nanoseconds& time) noexcept;
    void set_event_count(const uint64_t count = 0) noexcept;
    void add_event_count(const uint64_t count = 1) noexcept;
    void set_rtt_count(const uint64_t count = 0) noexcept;
    void set_band_count(const uint64_t count = 0) noexcept;

    [[nodiscard]] std::chrono::nanoseconds get_all_time() const noexcept;
    [[nodiscard]] double get_all_time_us() const noexcept;
    [[nodiscard]] double get_all_time_s() const noexcept;
    [[nodiscard]] double get_real_ave_time_us() const noexcept;
    [[nodiscard]] double get_real_ave_time_s() const noexcept;
  
    [[nodiscard]] uint64_t get_event_count() const noexcept;
    [[nodiscard]] double get_event_count_M() const noexcept;

    [[nodiscard]] double get_latency_us() const noexcept;
    [[nodiscard]] double get_throughput_Ops() const noexcept;
    [[nodiscard]] double get_throughput_MOps() const noexcept;
    [[nodiscard]] double get_bandwidth_GiBps(int64_t payload_byte) const noexcept;
    [[nodiscard]] double get_bandwidth_Gbps(int64_t payload_byte) const noexcept;
    [[nodiscard]] double get_bandwidth_MiBps(int64_t payload_byte) const noexcept;
    [[nodiscard]] double get_bandwidth_Mbps(int64_t payload_byte) const noexcept;

    [[nodiscard]] str time_event_str() const noexcept;
    [[nodiscard]] str rtt_str() const noexcept;
    [[nodiscard]] str band_str() const noexcept;
    [[nodiscard]] str throughput_latency_str() const noexcept;
    [[nodiscard]] str result_str() const noexcept;
};

}