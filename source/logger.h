// Logging of info/warnings/errors and simple profiling timers. Every render writes
// the collected messages to a .log file next to the output image.
#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

class Logger {
public:
    enum class Level { Info, Warning, Error };

    void info(const std::string& msg) { add(Level::Info, msg); }
    void warning(const std::string& msg) { add(Level::Warning, msg); }
    void error(const std::string& msg) { add(Level::Error, msg); }

    int warningCount() const { return warnings_; }
    int errorCount() const { return errors_; }
    void setQuiet(bool q) { quiet_ = q; }

    // Writes all messages to `path`. Returns false on I/O failure.
    bool writeTo(const std::string& path) const;

private:
    void add(Level lvl, const std::string& msg);
    std::vector<std::string> lines_;
    int warnings_ = 0, errors_ = 0;
    bool quiet_ = false;
    mutable std::mutex mutex_;
};

// Wall-clock stopwatch used for per-stage profiling.
class Timer {
public:
    Timer() : start_(std::chrono::steady_clock::now()) {}
    void reset() { start_ = std::chrono::steady_clock::now(); }
    double seconds() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};
