#include "ycsb/ycsb-fileloader.hpp"

namespace YCSB {

void split(const str& str, vec<viw>& result, char split_char, char end_char) {
    auto end_pos = str.find(end_char);
    auto pos = str.find(split_char);
    str::size_type init_pos = 0;
    result.clear();
    while (pos != str::npos) {
        if (pos >= end_pos) {
            pos = str::npos;
            break;
        }
        result.push_back(viw(str).substr(init_pos, pos - init_pos));
        init_pos = pos + 1;
        pos = str.find(split_char, init_pos);
    }
    result.push_back(viw(str).substr(init_pos, std::min(pos, str.size()) - init_pos + 1));
}

bool FileLoader::load_from_file(const str& file_path, uint64_t max_length) {

    if (file_path.empty()) {
        log_error << "file path is empty" << std::endl;
        return false;
    }

    vec<viw> file_path_result;
    split(file_path, file_path_result, '/');
    bool use_int64_key = (file_path_result.back()[0] != 'm');
    if (use_int64_key) {
        log_info << "use int64 key" << std::endl;
    } else {
        log_info << "use string key" << std::endl;
    }

    auto cache_bin_name = file_path + str(CACHE_BIN_SUFFIX);
    auto cache_bin_buffer_name = file_path + str(CACHE_BIN_BUFFER_SUFFIX);
    std::ifstream input_file_bin(cache_bin_name, std::ios::binary);
    buffer_block.read_from_file(cache_bin_buffer_name);
    if (input_file_bin.is_open() && buffer_block.is_data_valid()) {
        log_info << "use binary cache " << cache_bin_name << " instead" << std::endl;
        log_info << "use binary cache (buffer) " << cache_bin_buffer_name << " instead" << std::endl;
        uint64_t record_count = 0;
        input_file_bin.read(reinterpret_cast<char*>(&record_count), sizeof(record_count));
        file_savings.resize(record_count);
        input_file_bin.read(reinterpret_cast<char*>(file_savings.data()), file_savings.size() * sizeof(saving));
        log_debug << "read " << file_savings.size() << " data from " << cache_bin_name << std::endl;
        input_file_bin.close();
        for (auto& [op, key, value, end_key] : file_savings) {
            file_records.push_back(std::make_tuple(op, buffer_block.get_span(key), buffer_block.get_span(value), buffer_block.get_span(end_key)));
            if (file_records.size() >= max_length)
                break;
        }
        log_info << "use size = " << file_records.size() << " from " << file_path << std::endl;
        // delete file_savings
        file_savings.clear();
        return true;
    }
    input_file_bin.close();

    std::ifstream input_file;
    input_file.open(file_path);
    if (!input_file.is_open()) {
        log_error << "cannot open file " << file_path << std::endl;
        return false;
    }
    input_file.close();

    FILE * ifile = fopen(file_path.c_str(), "r");
    size_t linesz = 0;
    size_t readsz = 0;
    char * line_ch = nullptr;

    // in ycsb original file
    // data order is like this:
    // 1) useless data
    // 2) INSERT/READ/UPDATE/SCAN <table name> <key name> [ <field name>=<field data> <field name>=<field data> ... ]
    // 3) useless data

    uint64_t length = -1;
    // std::map<str, operat> quick_match = {
    //     {"INSERT", operat::insert}, {"READ", operat::read},     {"UPDATE", operat::update},
    //     {"SCAN", operat::scan},     {"DELETE", operat::remove},
    // };
    while ((readsz = getline(&line_ch, &linesz, ifile)) != -1) {

        if (readsz > 0 && line_ch[readsz - 1] == '\n') {
            line_ch[readsz - 1] = '\0';
        }

        str line(line_ch);
        if (line.length() == 0)
            continue;

        // specify by "fieldlength"
        if (length == -1 && viw(line).substr(0, 13) == "\"fieldlength\"") {
            auto len_data = viw(line).substr(15, line.length() - 1);  // strip left <fieldlength="> and strip right <">
            auto [ptr, err] = std::from_chars(len_data.data(), len_data.data() + len_data.size(), length);
        }

        // split to: INSERT/UPDATE-----usertable-----user6284781860667377211-----[ field0=:S%9C$TmC9?D=$ ]
        //                 0                1                   2                           3
        //           READ-----usertable-----user6174146320310297054-----[ <all fields>]
        //             0           1                   2                      3
        //           SCAN-----usertable-----user6174146320310297054-----15-----[ <all fields>]
        //             0           1                   2                3           4
        //           DELETE-----usertable-----user6174146320310297054
        //             0             1                   2

        vec<viw> result;
        // cost 40% of the time
        split(line, result, ' ', '[');

        if (result.size() <= 2)
            continue;
        // if (!quick_match.contains(result.at(0)))
        //     continue;

        // fieldlength default is 100
        if (length == -1) {
            length = 100;
        }

        // auto op = quick_match.at(result.at(0));

        operat op;
        if (result.at(0) == "INSERT") {
            op = operat::insert;
        } else if (result.at(0) == "READ") {
            op = operat::read;
        } else if (result.at(0) == "UPDATE") {
            op = operat::update;
        } else if (result.at(0) == "SCAN") {
            op = operat::scan;
        } else if (result.at(0) == "DELETE") {
            op = operat::remove;
        } else {
            continue;
        }

        auto key = result.at(2);
        viw value = EMPTY_VALUE;
        auto search_count = 1ull;

        // scan
        if (result.size() == 5) {
            // all fields
            if (result.at(4) == ALL_FIELDS_HEAD_FLAG) {
                auto [ptr, err] = std::from_chars(result.at(3).data(), result.at(3).data() + result.at(3).size(), search_count);
                // field.emplace(ALL_FIELDS, EMPTY_VALUE);
                value = ALL_FIELDS;
            // some fields
            } else {
                auto [ptr, err] = std::from_chars(result.at(3).data(), result.at(3).data() + result.at(3).size(), search_count);
                auto data = result.at(4);
                data = viw(data).substr(2, data.size() - 4);
                /*
                while (!data.empty()) {
                    uint64_t pos = data.find(' ');
                    auto field_name = viw(data).substr(0, pos);
                    data = viw(data).substr(std::min(pos + 1, data.size()));
                    field.emplace(field_name, EMPTY_VALUE);
                }
                */
                value = data;
            }

        // insert, read, update,
        } else if (result.size() == 4) {
            // all fields (read)
            if (result.at(3) == ALL_FIELDS_HEAD_FLAG) {
                // field.emplace(ALL_FIELDS, EMPTY_VALUE);
                value = ALL_FIELDS;
            // some fields
            } else {
                auto data = result.at(3);
                data = viw(data).substr(2, data.size() - 4);
                /*
                // insert, update
                if (op == operat::insert || op == operat::update) {
                    while (!data.empty()) {
                        uint64_t pos = data.find('=');
                        auto field_name = viw(data).substr(0, pos);
                        auto field_value = viw(data).substr(pos + 1, length);
                        data = viw(data).substr(std::min(pos + length + 2, data.size()));
                        field.emplace(field_name, field_value);
                    }
                // read
                } else {
                    while (!data.empty()) {
                        uint64_t pos = data.find(' ');
                        auto field_name = viw(data).substr(0, pos);
                        data = viw(data).substr(std::min(pos + 1, data.size()));
                        field.emplace(field_name, EMPTY_VALUE);
                    }
                    
                }
                */
                value = data;
            }
            // remove
        } else if (result.size() == 3) {
            // nothing to do
        }

        // specified below
        auto r = make_saving_from_strviw(op, key, value, search_count, result.size() == 5, use_int64_key);
        file_savings.push_back(r);
        // don't do this now: buffer_block will change store position when push_back
        // file_records.push_back(std::make_tuple(
        //     std::get<0>(r),
        //     buffer_block.get_span(std::get<1>(r)),
        //     buffer_block.get_span(std::get<2>(r)),
        //     std::get<3>(r)
        // ));

        if (file_savings.size() % 200000 == 0)
            log_debug << "read " << file_savings.size() << " data from " << file_path << std::endl;
    }

    free(line_ch);
    fclose(ifile);

    log_info << "writing cache to " << cache_bin_buffer_name << std::endl;
    buffer_block.write_to_file(cache_bin_buffer_name);
    if (!buffer_block.is_data_valid()) {
        log_error << "write cache error: could not open file " << cache_bin_buffer_name << std::endl;
        // delete file_savings
        file_savings.clear();
        return true;
    }
    std::fstream g(cache_bin_name, std::ios::out | std::ios::binary);
    if (!g) {
        log_error << "write cache error: could not open file " << cache_bin_name << std::endl;
        // delete file_savings
        file_savings.clear();
        return true;
    }
    uint64_t record_count = file_savings.size();
    log_info << "writing cache to " << cache_bin_name << std::endl;
    g.write(reinterpret_cast<const char*>(&record_count), sizeof(record_count));
    g.write(reinterpret_cast<const char*>(file_savings.data()), file_savings.size() * sizeof(saving));
    g.close();

    for (auto& [op, key, value, end_key] : file_savings) {
        file_records.push_back(std::make_tuple(op, buffer_block.get_span(key), buffer_block.get_span(value), buffer_block.get_span(end_key)));
        if (file_records.size() >= max_length)
            break;
    }
    log_info << "use size = " << file_records.size() << " from " << file_path << std::endl;

    // delete file_savings
    file_savings.clear();

    return true;
}

FileLoader::saving FileLoader::make_saving_from_strviw(operat op, viw key, viw value, uint64_t search_count, bool use_scan, bool use_int64_key) {

    uint64_t key_start = 0, key_size = 0;
    uint64_t end_key_start = 0, end_key_size = 0;
    if (use_int64_key) {
        uint64_t new_key = 0;
        auto key_real_part = viw(key).substr(4);
        auto [ptr, err] = std::from_chars(key_real_part.data(), key_real_part.data() + key_real_part.size(), new_key);
        key_start = buffer_block.size();
        key_size = sizeof(new_key);
        buffer_block.add_u64(new_key);
        if (use_scan) {
            auto new_end_key = new_key + search_count;
            end_key_start = buffer_block.size();
            end_key_size = sizeof(new_end_key);
            buffer_block.add_u64(new_end_key);
        } 
    } else {
        key_start = buffer_block.size();
        key_size = key.size() + 1;
        buffer_block.add_strviw_more_0(key);
        // TODO: scan update email
        if (use_scan) {
            end_key_start = key_start;
            end_key_size = key_size;
        }
    }

    auto value_start = buffer_block.size();
    auto value_size = value.size();
    buffer_block.add_strviw(value);

    return std::make_tuple(op, key::save_t{key_start, key_size}, key::save_t{value_start, value_size}, key::save_t{end_key_start, end_key_size});
}

// get all records length
uint64_t FileLoader::get_record_len() const noexcept {
    return file_records.size();
}

// part from 0 to all_parts - 1
uint64_t FileLoader::get_part_len(uint64_t part, uint64_t all_parts) const noexcept {
    return get_part_index_end(part, all_parts) - get_part_index_start(part, all_parts);
}

// part from 0 to all_parts - 1
uint64_t FileLoader::get_part_index_start(uint64_t part, uint64_t all_parts) const noexcept {
    if (part < 0 || all_parts <= 0 || part >= all_parts)
        return file_records.size();  // end
    auto my_start = (file_records.size() + all_parts - 1) / all_parts * (part);
    if (my_start >= file_records.size()) my_start = file_records.size();  // end
    return my_start;
}

// part from 0 to all_parts - 1
uint64_t FileLoader::get_part_index_end(uint64_t part, uint64_t all_parts) const noexcept {
    if (part < 0 || all_parts <= 0 || part >= all_parts)
        return file_records.size();  // end
    auto my_end = (file_records.size() + all_parts - 1) / all_parts * (part + 1);
    if (my_end >= file_records.size()) my_end = file_records.size();  // end
    return my_end;
}

// part from 0 to all_parts - 1
FileLoader::records::iterator FileLoader::get_part_iter_start(uint64_t part, uint64_t all_parts) noexcept {
    return file_records.begin() + this->get_part_index_start(part, all_parts);
}

// part from 0 to all_parts - 1
FileLoader::records::iterator FileLoader::get_part_iter_end(uint64_t part, uint64_t all_parts) noexcept {
    return file_records.begin() + this->get_part_index_end(part, all_parts);
}

}
