#pragma once

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace stdlog {

enum class color : int {
    reset = 0, bold, underline = 4,
    dark_grey = 30, dark_red, dark_green, dark_yellow,
    dark_blue, dark_purple, dark_cyan, dark_white,
    grey = 90, red, green, yellow,
    blue, purple, cyan, white
};

class Color {
private:
    std::vector<color> all;

public:
    Color() = default;
    ~Color() = default;

    Color(const Color& other) : all(other.all) {}
    Color(const color& c) : all({c}) {}
    Color(const int& value) : all({static_cast<color>(value)}) {}

    [[nodiscard]] std::string to_string() const {
        if (all.empty()) return "";
        std::string res = "\033[" + std::to_string(int(all.front()));
        // must use string. if only use int, std::hex or other things will break it
        for (auto i = all.begin() + 1; i != all.end(); ++i)
           res += ";" + std::to_string(int(*i));
        res += "m";
        return res;
    }

    [[nodiscard]] Color operator+(const Color& another) const {
        Color result(*this);
        result.all.insert(result.all.end(), another.all.begin(), another.all.end());
        return result;
    }

    [[nodiscard]] Color operator+(const color& data) const {
        Color result(*this);
        result.all.push_back(data);
        return result;
    }

    [[nodiscard]] Color operator+(const int& data) const {
        Color result(*this);
        result.all.push_back(static_cast<color>(data));
        return result;
    }

};

inline Color operator+(const color& data1, const color& data2) {
    Color result(data1);
    return result + data2;
}

class LogEndInfo {};
extern LogEndInfo end_info;

class LogEndLine {};
extern LogEndLine endl;

class Logger {
  private:
    Color color;
    Color color_after_stream_info;
    bool begin = false;

  public:
    Logger(Color color) : color(color) {}
    Logger(Color color, Color color_after_stream_info) : color(color), color_after_stream_info(color_after_stream_info) {}
    ~Logger() = default;

    template <typename T>
    Logger& operator<<(const T& output) {
        if (!begin) {
            std::cout << color.to_string();
            begin = true;
        }
        std::cout << output;
        return *this;
    }

    Logger& operator<<(const LogEndInfo& output) {
        std::cout << color_after_stream_info.to_string();
        return *this;
    }

    Logger& operator<<(const LogEndLine& endl) {
        std::cout << Color(color::reset).to_string() << std::endl;
        begin = false;
        return *this;
    }

    Logger& operator<<(decltype(std::endl<char, std::char_traits<char>>)& endl) {
        return this->operator<<(stdlog::endl);
    }

};

extern Logger debug;  // grey
extern Logger info;  // blue
extern Logger warn;  // yellow
extern Logger error;  // red
extern Logger green;
extern Logger cyan;
extern Logger purple;
extern Logger white;

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define __STREAM_INFO__ "[" << __FILENAME__ << ":" << __LINE__ << "] (" << __FUNCTION__ << ") "
#define __STREAM_INFO_WITH_END__ __STREAM_INFO__ << stdlog::end_info

#define log_debug stdlog::debug <<__STREAM_INFO_WITH_END__
#define log_info stdlog::info <<__STREAM_INFO_WITH_END__
#define log_warn stdlog::warn <<__STREAM_INFO_WITH_END__
#define log_error stdlog::error <<__STREAM_INFO_WITH_END__
#define log_green stdlog::green <<__STREAM_INFO_WITH_END__
#define log_cyan stdlog::cyan <<__STREAM_INFO_WITH_END__
#define log_purple stdlog::purple <<__STREAM_INFO_WITH_END__
#define log_white stdlog::white <<__STREAM_INFO_WITH_END__

}

// in stdlog.cc you should add such as
/*
namespace stdlog {
LogEndInfo end_info;
LogEndLine endl;
Logger debug(color::grey);
Logger info(color::blue, color::bold);
Logger warn(color::yellow, color::bold + color::underline);
Logger error(color::red, color::bold + color::underline);
Logger green(color::green);
Logger cyan(color::cyan);
Logger purple(color::purple);
Logger white(color::white);
}
*/