#include "logger.h"

#include <fstream>
#include <iostream>

void Logger::add(Level lvl, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* tag = "[INFO]    ";
    if (lvl == Level::Warning) { tag = "[WARNING] "; ++warnings_; }
    if (lvl == Level::Error) { tag = "[ERROR]   "; ++errors_; }
    std::string line = std::string(tag) + msg;
    lines_.push_back(line);
    if (lvl != Level::Info)
        std::cerr << line << "\n";
    else if (!quiet_)
        std::cout << line << "\n";
}

bool Logger::writeTo(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream f(path);
    if (!f) return false;
    for (const auto& l : lines_) f << l << "\n";
    f << "[SUMMARY] " << warnings_ << " warning(s), " << errors_ << " error(s)\n";
    return (bool)f;
}
