#include "log/stdlog.hpp"

namespace stdlog {

LogEndInfo end_info;
LogEndLine endl;

Logger debug(color::grey, color::bold);
Logger info(color::blue, color::bold);
Logger warn(color::yellow, color::bold + color::underline);
Logger error(color::red, color::bold + color::underline);
Logger green(color::green, color::bold);
Logger cyan(color::cyan, color::bold);
Logger purple(color::purple, color::bold);
Logger white(color::white, color::bold);

}