#include "measure/timecounter.hpp"
#include "measure/literal.hpp"

namespace counter {

TimeCounter::TimeCounter() noexcept {
    reset_time_counter();
}

void TimeCounter::reset_time_counter() noexcept {
    time_cost = 0ns;
    event_count = 0;
}

void TimeCounter::start() noexcept {
    start_point = std::chrono::steady_clock::now();
}

void TimeCounter::stop() noexcept {
    auto end_point = std::chrono::steady_clock::now();
    time_cost += std::chrono::duration_cast<std::chrono::nanoseconds>(end_point - start_point);
}

void TimeCounter::set_thread_num(const int thread_num) noexcept {
    this->thread_num = thread_num;
}

void TimeCounter::set_all_time_cost(const std::chrono::nanoseconds& time) noexcept {
    time_cost = time;
}

void TimeCounter::add_all_time_cost(const std::chrono::nanoseconds& time) noexcept {
    time_cost += time;
}

void TimeCounter::set_event_count(const uint64_t count) noexcept {
    event_count = count;
}

void TimeCounter::add_event_count(const uint64_t count) noexcept {
    event_count += count;
}

void TimeCounter::set_rtt_count(const uint64_t count) noexcept {
    rtt_count = count;
}

void TimeCounter::set_band_count(const uint64_t count) noexcept {
    band_count = count;
}

std::chrono::nanoseconds TimeCounter::get_all_time() const noexcept {
    return time_cost;
}

double TimeCounter::get_all_time_us() const noexcept {
    return time_cost.count() * ns_to_us;
}

double TimeCounter::get_all_time_s() const noexcept {
    return (double)time_cost.count() * ns_to_s;
}

double TimeCounter::get_real_ave_time_us() const noexcept {
    return get_all_time_us() / double(thread_num);
}

double TimeCounter::get_real_ave_time_s() const noexcept {
    return get_all_time_s() / double(thread_num);
}

uint64_t TimeCounter::get_event_count() const noexcept {
    return event_count;
}

double TimeCounter::get_event_count_M() const noexcept {
    return double(event_count) / 1_M;
}

double TimeCounter::get_latency_us() const noexcept {
    if (event_count != 0)
        return get_all_time_us() / double(event_count);
    return 0;
}

double TimeCounter::get_throughput_Ops() const noexcept {
    if (time_cost.count() != 0)
        return double(event_count) / get_real_ave_time_s();
    return 0;
}

double TimeCounter::get_throughput_MOps() const noexcept {
    if (time_cost.count() != 0)
        return double(event_count) / get_real_ave_time_s() / 1_M;
    return 0;
}

double TimeCounter::get_bandwidth_GiBps(int64_t payload_byte) const noexcept {
    return get_throughput_Ops() * (double)payload_byte * Bps_to_GiBps;
}

double TimeCounter::get_bandwidth_Gbps(int64_t payload_byte) const noexcept {
    return get_throughput_Ops() * (double)payload_byte * Bps_to_Gbps;
}

double TimeCounter::get_bandwidth_MiBps(int64_t payload_byte) const noexcept {
    return get_throughput_Ops() * (double)payload_byte * Bps_to_MiBps;
}

double TimeCounter::get_bandwidth_Mbps(int64_t payload_byte) const noexcept {
    return get_throughput_Ops() * (double)payload_byte * Bps_to_Mbps;
}

str TimeCounter::time_event_str() const noexcept {
    std::stringstream s;
    // s << "average time cost: " << get_real_ave_time_us() << " us";
    // s << "average time cost: " << get_all_time_us() / get_event_count_M() / 1_M << " us";
    s << "total event count: " << get_event_count_M() << " M";
    return s.str();
}

str TimeCounter::rtt_str() const noexcept {
    std::stringstream s;
    s << "avg rtt count: " << double(rtt_count) / get_event_count();
    return s.str();
}

str TimeCounter::band_str() const noexcept {
    std::stringstream s;
    s << "avg bandwidth consumption: " << double(band_count) / get_event_count();
    return s.str();
}

str TimeCounter::throughput_latency_str() const noexcept {
    std::stringstream s;
    s << "throughput: " << get_throughput_MOps() << " MOps; ";
    s << "latency: " << get_latency_us() << " us";
    return s.str();
}

str TimeCounter::result_str() const noexcept {
    return time_event_str() + "; " + rtt_str() + "; " + band_str() + "; " + throughput_latency_str();
}

}