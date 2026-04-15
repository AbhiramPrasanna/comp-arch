#pragma once

#include <measure/measure.hpp>

namespace YCSB {

constexpr viw ALL_FIELDS_HEAD_FLAG = "[ <all fields>]";
constexpr viw ALL_FIELDS = "<all fields>";
constexpr viw EMPTY_VALUE = "";

constexpr viw CACHE_BIN_SUFFIX = "__bin_";
constexpr viw CACHE_BIN_BUFFER_SUFFIX = "__bin_buffer_";

// key operator
enum class operat { insert, read, update, scan, remove };

}
