#pragma once

// use int8_t and so on
#include <cstddef>
#include <cstdint>

// some stl
#include <span>
#include <functional>
#include <map>
#include <string>
#include <sstream>
#include <tuple>
#include <vector>
#include <queue>

// ptrs
#include <memory>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

// short names
using str = std::string;
using viw = std::string_view;
using span = std::span<u8>;
using std::map;
template <typename T>
using vec = std::vector<T>;
template <typename... Ts>
using tup = std::tuple<Ts...>;
template <typename T>
using func = std::function<T>;

// compound names
template <typename... Ts>
using voidfunc = func<void(Ts...)>;