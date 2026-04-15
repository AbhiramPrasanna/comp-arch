#include "ycsb/ycsb-timecounter.hpp"

namespace YCSB {

// part from 0 to all_parts - 1
void Benchmark::prepare_workload_file(FileLoader& file_loader, uint64_t part, uint64_t all_parts) {
    this->start_record = this->now_record = file_loader.get_part_iter_start(part, all_parts);
    this->end_record = file_loader.get_part_iter_end(part, all_parts);
}

void Benchmark::start_benchmark() {

    for (this->now_record = this->start_record; this->now_record != this->end_record; ++this->now_record) {

        str read_result;
        vec<str> scan_result;

        auto& i = *this->now_record;

        switch (std::get<0>(i)) {
        case operat::insert:
            benchmark_insert(std::get<1>(i), std::get<2>(i));
            event_count++;
            event_count_scan_use_multi++;
            break;
        case operat::update:
            benchmark_update(std::get<1>(i), std::get<2>(i));
            event_count++;
            event_count_scan_use_multi++;
            break;
        case operat::read: {
            // std::get<2> is field, not used here
            benchmark_read(std::get<1>(i), read_result);
            event_count++;
            event_count_scan_use_multi++;
            break;
        }
        case operat::scan: {
            // std::get<2> is field, not used here
            benchmark_scan(std::get<1>(i), std::get<3>(i), scan_result);
            event_count++;
            event_count_scan_use_multi += scan_result.size();
            break;
        }
        case operat::remove:
            benchmark_remove(std::get<1>(i));
            event_count++;
            event_count_scan_use_multi++;
            break;
        }
    }
}

uint64_t Benchmark::get_event_count_scan_use_multi() const noexcept {
    return event_count_scan_use_multi;
}

uint64_t Benchmark::get_event_count() const noexcept {
    return event_count;
}

void Benchmark::benchmark_insert(span key, span values) {
    insert_func(key, values);
}

void Benchmark::benchmark_update(span key, span values) {
    update_func(key, values);
}

void Benchmark::benchmark_read(span key, str& result) {
    read_func(key, result);
}

void Benchmark::benchmark_scan(span start_key, span end_key, vec<str>& result_vec) {
    scan_func(start_key, end_key, result_vec);
}

void Benchmark::benchmark_remove(span key) {
    remove_func(key);
}


#define TIME_WRAP_(message) \
    do { \
        auto start = std::chrono::system_clock::now(); \
        message; \
        auto end = std::chrono::system_clock::now(); \
        time_cost += std::chrono::duration_cast<std::chrono::microseconds>(end - start); \
    } while (false)

void TimeCounter::benchmark_insert(span key, span values) {
    TIME_WRAP_(insert_func(key, values));
}

void TimeCounter::benchmark_update(span key, span values) {
    TIME_WRAP_(update_func(key, values));
}

void TimeCounter::benchmark_read(span key, str& result) {
    TIME_WRAP_(read_func(key, result));
}

void TimeCounter::benchmark_scan(span start_key, span end_key, vec<str>& result_vec) {
    TIME_WRAP_(scan_func(start_key, end_key, result_vec));
}

void TimeCounter::benchmark_remove(span key) {
    TIME_WRAP_(remove_func(key));
}


void Benchmark::register_insert(voidfunc<span, span> func) {
    insert_func = std::move(func);
}
void Benchmark::register_update(voidfunc<span, span> func) {
    update_func = std::move(func);
}
void Benchmark::register_read(voidfunc<span, str&> func) {
    read_func = std::move(func);
}
void Benchmark::register_scan(voidfunc<span, span, vec<str>&> func) {
    scan_func = std::move(func);
}
void Benchmark::register_remove(voidfunc<span> func) {
    remove_func = std::move(func);
}


}