#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <filesystem>

namespace VirtualOverlay {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static Logger& Instance();

    // Initialize with default log directory (%LOCALAPPDATA%\VirtualOverlay\logs)
    bool Init();
    
    // Initialize with custom log directory
    bool Init(const std::wstring& logDir);
    
    void Shutdown();

    void SetMinLevel(LogLevel level);
    LogLevel GetMinLevel() const;

    void Debug(const char* format, ...);
    void Info(const char* format, ...);
    void Warn(const char* format, ...);
    void Error(const char* format, ...);

    void Log(LogLevel level, const char* format, ...);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void LogImpl(LogLevel level, const char* format, va_list args);
    void RotateIfNeeded();
    std::string GetTimestamp();
    std::string LevelToString(LogLevel level);
    void EnsureLogDirectory();

    std::ofstream m_file;
    std::recursive_mutex m_mutex;
    std::wstring m_logDir;
    std::wstring m_currentLogPath;
    LogLevel m_minLevel = LogLevel::Debug;
    int m_currentDay = -1;
    size_t m_maxFileSize = 10 * 1024 * 1024;  // 10 MB
    int m_maxDaysToKeep = 7;
    bool m_initialized = false;
};

// Convenience macros
#define LOG_DEBUG(...) VirtualOverlay::Logger::Instance().Debug(__VA_ARGS__)
#define LOG_INFO(...)  VirtualOverlay::Logger::Instance().Info(__VA_ARGS__)
#define LOG_WARN(...)  VirtualOverlay::Logger::Instance().Warn(__VA_ARGS__)
#define LOG_ERROR(...) VirtualOverlay::Logger::Instance().Error(__VA_ARGS__)

} // namespace VirtualOverlay
