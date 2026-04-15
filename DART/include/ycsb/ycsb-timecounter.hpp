#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ranges>
#include <utility>
#include <thread>

#include "boost/coroutine2/all.hpp"

#include "measure/measure.hpp"
#include "log/log.hpp"

#include "ycsb/ycsb-head.hpp"
#include "ycsb/ycsb-fileloader.hpp"



template<typename T>
using coro = boost::coroutines2::coroutine<T>;

namespace YCSB {

class Benchmark {
  private:
    uint64_t event_count = 0;
    uint64_t event_count_scan_use_multi = 0;
    FileLoader::records::iterator now_record, start_record, end_record;

  protected:
    voidfunc<span, span> insert_func;
    voidfunc<span, span> update_func;
    voidfunc<span, str&> read_func;
    voidfunc<span, span, vec<str>&> scan_func;
    voidfunc<span> remove_func;

  public:
    Benchmark() = default;
    ~Benchmark() = default;

    virtual void register_insert(voidfunc<span, span>);
    virtual void register_update(voidfunc<span, span>);
    virtual void register_read  (voidfunc<span, str&>);
    virtual void register_scan  (voidfunc<span, span, vec<str>&>);
    virtual void register_remove(voidfunc<span>);

    void prepare_workload_file(FileLoader& file_loader, uint64_t part = 1, uint64_t all_parts = 1);
    void start_benchmark();
    uint64_t get_event_count() const noexcept;
    uint64_t get_event_count_scan_use_multi() const noexcept;


  private:
    virtual void benchmark_insert(span key, span value);
    virtual void benchmark_update(span key, span value);
    virtual void benchmark_read(span key, str& result);
    virtual void benchmark_scan(span start_key, span end_key, vec<str>& result_vec);
    virtual void benchmark_remove(span key);
};

class TimeCounter : public counter::TimeCounter, public Benchmark {
  public:
    TimeCounter() = default;
    ~TimeCounter() = default;

  private:
    void benchmark_insert(span key, span values);
    void benchmark_update(span key, span values);
    void benchmark_read(span key, str& result);
    void benchmark_scan(span start_key, span end_key, vec<str>& result_vec);
    void benchmark_remove(span key);
};

}
