// sp/str.h
// String utilities

#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>

namespace sp {
    namespace str {

        inline std::string to_hex(uint64_t value)
        {
            std::ostringstream oss;
            oss << std::hex << std::uppercase << value;
            return oss.str();
        }

        inline std::string format(const char* fmt, ...)
        {
            char buf[4096];
            va_list args;
            va_start(args, fmt);
            vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);
            return std::string(buf);
        }

    } // namespace str
} // namespace sp