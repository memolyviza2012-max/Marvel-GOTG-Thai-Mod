// sp/file.h
// File I/O utilities - narrow-char for ANSI/MultiByte build

#pragma once
#include <fstream>
#include <string>

namespace sp {
    namespace io {

        class ps_ostream {
        public:
            ps_ostream(const std::string& name = "Debug") : name_(name), log_file_("") {}

            void set_log_file(const std::string& path) { log_file_ = path; }
            void start() { active_ = true; }

            void print(const std::string& msg)
            {
                if (!log_file_.empty())
                {
                    std::ofstream f(log_file_, std::ios::app);
                    if (f.good())
                        f << msg;
                }
            }

        private:
            std::string name_;
            std::string log_file_;
            bool active_ = false;
        };

    } // namespace io
} // namespace sp