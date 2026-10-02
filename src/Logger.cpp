#include "Logger.h"
#include <iomanip>

std::wstring Logger::s_logFilePath;
std::wofstream Logger::s_logFile;
std::mutex Logger::s_mutex;
bool Logger::s_initialized = false;

/**
 * @brief Convert log level enumeration to readable text.
 */
const wchar_t* Logger::levelToString(Level level) {
    switch (level) {
        case Level::Debug:   return L"DEBUG";
        case Level::Info:    return L"INFO";
        case Level::Warning: return L"WARN";
        case Level::Error:   return L"ERROR";
        default:             return L"UNKNOWN";
    }
}

/**
 * @brief Initializes the logging subsystem.
 */
void Logger::init(const std::wstring& logFilePath) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_logFilePath = logFilePath;
    s_logFile.open(logFilePath, std::ios::out | std::ios::app);
    s_initialized = s_logFile.is_open();
}

/**
 * @brief Records a log entry to file and Windows debug stream.
 */
void Logger::log(Level level, const char* file, int line, const std::wstring& message) {
    std::lock_guard<std::mutex> lock(s_mutex);

    // Extract basename from source file path
    const char* baseFile = file;
    const char* p = file;
    while (*p) {
        if (*p == '\\' || *p == '/') {
            baseFile = p + 1;
        }
        p++;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);

    std::wstringstream ss;
    ss << L"["
       << std::setfill(L'0') << std::setw(4) << st.wYear << L"-"
       << std::setfill(L'0') << std::setw(2) << st.wMonth << L"-"
       << std::setfill(L'0') << std::setw(2) << st.wDay << L" "
       << std::setfill(L'0') << std::setw(2) << st.wHour << L":"
       << std::setfill(L'0') << std::setw(2) << st.wMinute << L":"
       << std::setfill(L'0') << std::setw(2) << st.wSecond << L"."
       << std::setfill(L'0') << std::setw(3) << st.wMilliseconds
       << L"] [" << levelToString(level) << L"] ["
       << baseFile << L":" << line << L"] "
       << message << L"\n";

    std::wstring entry = ss.str();

    // Output to Visual Studio / debugger console
    OutputDebugStringW(entry.c_str());

    // Output to log file if available
    if (s_initialized && s_logFile.is_open()) {
        s_logFile << entry;
        s_logFile.flush();
    }
}

/**
 * @brief Flushes any pending output to disk.
 */
void Logger::flush() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_initialized && s_logFile.is_open()) {
        s_logFile.flush();
    }
}

/**
 * @brief Gracefully terminates logging.
 */
void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_initialized && s_logFile.is_open()) {
        s_logFile.close();
        s_initialized = false;
    }
}
