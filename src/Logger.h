#pragma once
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <sstream>

/**
 * @brief Thread-safe file and debug console logger for Quicky.
 * 
 * Provides persistent logging to quicky.log and OutputDebugStringW
 * to diagnose crashes, drag-and-drop operations, and clipboard actions.
 */
class Logger {
public:
    enum class Level {
        Debug,
        Info,
        Warning,
        Error
    };

    /**
     * @brief Initializes the logging system with the given log file path.
     * @param logFilePath Full path to the log file.
     */
    static void init(const std::wstring& logFilePath);

    /**
     * @brief Writes a formatted log entry.
     * @param level Severity level of the log entry.
     * @param file Source file name where the log originated.
     * @param line Source line number where the log originated.
     * @param message Text message to be logged.
     */
    static void log(Level level, const char* file, int line, const std::wstring& message);

    /**
     * @brief Flushes any pending log buffers.
     */
    static void flush();

    /**
     * @brief Closes the log file handle.
     */
    static void shutdown();

private:
    static std::wstring s_logFilePath;
    static std::wofstream s_logFile;
    static std::mutex s_mutex;
    static bool s_initialized;

    static const wchar_t* levelToString(Level level);
};

#define LOG_DEBUG(msg) Logger::log(Logger::Level::Debug, __FILE__, __LINE__, msg)
#define LOG_INFO(msg)  Logger::log(Logger::Level::Info,  __FILE__, __LINE__, msg)
#define LOG_WARN(msg)  Logger::log(Logger::Level::Warning, __FILE__, __LINE__, msg)
#define LOG_ERROR(msg) Logger::log(Logger::Level::Error, __FILE__, __LINE__, msg)
