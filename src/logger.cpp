#include "logger.h"
#include <iostream>
#include <ctime>

Logger::Logger()
    : min_level_(LogLevel::INFO)
    , console_enabled_(false)
    , file_enabled_(false)
    , debug_mode_(false) {
}

Logger::~Logger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::set_log_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

void Logger::enable_file_logging(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_.is_open()) {
        log_file_.close();
    }
    log_file_.open(filename, std::ios::out | std::ios::app);
    if (log_file_.is_open()) {
        file_enabled_ = true;
        log_file_ << "\n=== Logging session started at " << get_timestamp() << " ===\n";
        log_file_.flush();
    } else {
        std::cerr << "Failed to open log file: " << filename << std::endl;
        file_enabled_ = false;
    }
}

void Logger::enable_console_logging(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    console_enabled_ = enable;
}

void Logger::set_debug_mode(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    debug_mode_ = enable;
    if (enable) {
        min_level_ = LogLevel::DEBUG;
    }
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_r(&time_t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO ";
        case LogLevel::WARNING: return "WARN ";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

bool Logger::should_log(LogLevel level) {
    if (!debug_mode_ && level == LogLevel::DEBUG) {
        return false;
    }
    return static_cast<int>(level) >= static_cast<int>(min_level_);
}

void Logger::log(LogLevel level, const std::string& message,
                 const char* file, int line) {
    if (!should_log(level)) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream log_line;
    log_line << "[" << get_timestamp() << "] "
             << "[" << level_to_string(level) << "] "
             << message;

    // Add file and line info for debug messages
    if (level == LogLevel::DEBUG && file != nullptr) {
        // Extract just the filename from the full path
        std::string filename(file);
        size_t last_slash = filename.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            filename = filename.substr(last_slash + 1);
        }
        log_line << " (" << filename << ":" << line << ")";
    }

    std::string final_message = log_line.str();

    // Write to console if enabled
    if (console_enabled_) {
        std::cout << final_message << std::endl;
    }

    // Write to file if enabled
    if (file_enabled_ && log_file_.is_open()) {
        log_file_ << final_message << std::endl;
        log_file_.flush(); // Ensure immediate write
    }
}

void Logger::debug(const std::string& message, const char* file, int line) {
    log(LogLevel::DEBUG, message, file, line);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARNING, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}
