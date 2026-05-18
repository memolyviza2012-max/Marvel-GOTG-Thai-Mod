// sp/environment.h
// Environment utilities - narrow-char for ANSI/MultiByte build

#pragma once
#include <windows.h>
#include <string>

namespace sp {
    namespace env {

        inline std::string lib_dir()
        {
            char path[MAX_PATH];
            GetModuleFileNameA(NULL, path, MAX_PATH);
            std::string p(path);
            size_t last_sep = p.find_last_of("\\/");
            if (last_sep != std::string::npos)
                p = p.substr(0, last_sep);
            return p;
        }

        inline std::string exe_path()
        {
            char path[MAX_PATH];
            GetModuleFileNameA(NULL, path, MAX_PATH);
            return std::string(path);
        }

        inline std::string exe_name()
        {
            std::string p = exe_path();
            size_t last_sep = p.find_last_of("\\/");
            if (last_sep != std::string::npos)
                p = p.substr(last_sep + 1);
            return p;
        }

    } // namespace env
} // namespace sp