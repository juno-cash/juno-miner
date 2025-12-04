#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <chrono>
#include <iomanip>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    static Logger& instance();

    // Configuration
    void set_log_level(LogLevel level);
    void enable_file_logging(const std::string& filename);
    void enable_console_logging(bool enable);
    void set_debug_mode(bool enable);

    // Logging methods
    void log(LogLevel level, const std::string& message,
             const char* file = nullptr, int line = 0);

    void debug(const std::string& message, const char* file = nullptr, int line = 0);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);

private:
    Logger();
    ~Logger();

    // Disable copy and assignment
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string get_timestamp();
    std::string level_to_string(LogLevel level);
    bool should_log(LogLevel level);

    LogLevel min_level_;
    bool console_enabled_;
    bool file_enabled_;
    bool debug_mode_;
    std::ofstream log_file_;
    std::mutex mutex_;
};

// Convenience macros for debug logging with file and line info
#define LOG_DEBUG(msg) Logger::instance().debug(msg, __FILE__, __LINE__)
#define LOG_INFO(msg) Logger::instance().info(msg)
#define LOG_WARNING(msg) Logger::instance().warning(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)

// Stream-style logging macros
#define LOG_DEBUG_STREAM(expr) \
    do { \
        std::ostringstream oss; \
        oss << expr; \
        Logger::instance().debug(oss.str(), __FILE__, __LINE__); \
    } while(0)

#define LOG_INFO_STREAM(expr) \
    do { \
        std::ostringstream oss; \
        oss << expr; \
        Logger::instance().info(oss.str()); \
    } while(0)

#define LOG_WARNING_STREAM(expr) \
    do { \
        std::ostringstream oss; \
        oss << expr; \
        Logger::instance().warning(oss.str()); \
    } while(0)

#define LOG_ERROR_STREAM(expr) \
    do { \
        std::ostringstream oss; \
        oss << expr; \
        Logger::instance().error(oss.str()); \
    } while(0)

#endif // LOGGER_H
