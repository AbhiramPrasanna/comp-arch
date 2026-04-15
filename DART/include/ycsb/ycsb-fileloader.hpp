#pragma once

#include <cstdio>
#include <fstream>
#include <ranges>
#include <utility>
#include <cstring>
#include <charconv>
#include <iostream>
#include <string_view>

#include "measure/measure.hpp"
#include "log/log.hpp"

#include "ycsb/ycsb-head.hpp"

namespace YCSB {

void split(
    const str& str, vec<viw>& result, char split_char = ' ', char end_char = '\n'
);

class FileLoader {
  public:
    // record
    // std::get<0>(): operator, values are operat::{insert, read, update, scan, remove}
    // std::get<1>(): key span<u8>
    // std::get<3>(): value span<u8>
    // std::get<4>(): end key span<u8>
    using record = tup<operat, span, span, span>;
    using saving = tup<operat, key::save_t, key::save_t, key::save_t>;
    // records: vector of record
    using records = vec<record>;
    using savings = vec<saving>;

    FileLoader() = default;
    ~FileLoader() = default;

    // must load
    bool load_from_file(const str& file_path, uint64_t max_length = -1);

    // get all records length
    [[nodiscard]] uint64_t get_record_len() const noexcept;

    // get <part / all_parts> partial of iterator
    [[nodiscard]] uint64_t get_part_len(uint64_t part = 0, uint64_t all_parts = 1) const noexcept;
    [[nodiscard]] uint64_t get_part_index_start(uint64_t part = 0, uint64_t all_parts = 1) const noexcept;
    [[nodiscard]] uint64_t get_part_index_end(uint64_t part = 0, uint64_t all_parts = 1) const noexcept;
    [[nodiscard]] records::iterator get_part_iter_start(uint64_t part = 0, uint64_t all_parts = 1) noexcept;
    [[nodiscard]] records::iterator get_part_iter_end(uint64_t part = 0, uint64_t all_parts = 1) noexcept;

  private:
    records file_records;  // a span view of {file_savings + buffer_block}
    savings file_savings;
    key_value_buffer::BufferBlock buffer_block;
    saving make_saving_from_strviw(operat op, viw key, viw value, uint64_t search_count, bool use_scan, bool use_int64_key);
};

}
