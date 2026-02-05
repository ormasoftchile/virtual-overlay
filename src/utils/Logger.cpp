#include "Logger.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdarg>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace VirtualOverlay {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    Shutdown();
}

bool Logger::Init() {
    // Get default log directory: %LOCALAPPDATA%\VirtualOverlay\logs
    wchar_t* appDataPath = nullptr;
    std::wstring logDir;
    
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appDataPath))) {
        logDir = appDataPath;
        CoTaskMemFree(appDataPath);
        logDir += L"\\VirtualOverlay\\logs";
    } else {
        // Fallback to current directory
        logDir = L".\\logs";
    }
    
    return Init(logDir);
}

bool Logger::Init(const std::wstring& logDir) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (m_initialized) return true;

    m_logDir = logDir;
    EnsureLogDirectory();
    RotateIfNeeded();
    m_initialized = true;

    // Log startup
    LogImpl(LogLevel::Info, "Logger initialized", nullptr);
    return true;
}

void Logger::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (!m_initialized) return;

    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }
    m_initialized = false;
}

void Logger::SetMinLevel(LogLevel level) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_minLevel = level;
}

LogLevel Logger::GetMinLevel() const {
    return m_minLevel;
}

void Logger::Debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogImpl(LogLevel::Debug, format, args);
    va_end(args);
}

void Logger::Info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogImpl(LogLevel::Info, format, args);
    va_end(args);
}

void Logger::Warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogImpl(LogLevel::Warn, format, args);
    va_end(args);
}

void Logger::Error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogImpl(LogLevel::Error, format, args);
    va_end(args);
}

void Logger::Log(LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogImpl(level, format, args);
    va_end(args);
}

void Logger::LogImpl(LogLevel level, const char* format, va_list args) {
    if (level < m_minLevel) return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (!m_initialized) return;

    RotateIfNeeded();

    if (!m_file.is_open()) return;

    // Format the message
    char buffer[4096];
    if (args) {
        vsnprintf(buffer, sizeof(buffer), format, args);
    } else {
        strncpy_s(buffer, format, sizeof(buffer) - 1);
    }

    // Write log line
    m_file << "[" << GetTimestamp() << "] "
           << "[" << std::setw(5) << LevelToString(level) << "] "
           << buffer << "\n";
    m_file.flush();

    // Also output to debug console in debug builds
#ifdef _DEBUG
    OutputDebugStringA(("[" + LevelToString(level) + "] " + buffer + "\n").c_str());
#endif
}

void Logger::RotateIfNeeded() {
    // Get current day
    time_t now = time(nullptr);
    tm localTime;
    localtime_s(&localTime, &now);
    int today = localTime.tm_yday;

    // Check if we need to rotate
    bool needsRotation = false;
    
    if (m_currentDay != today) {
        needsRotation = true;
        m_currentDay = today;
    } else if (m_file.is_open()) {
        // Check file size
        m_file.seekp(0, std::ios::end);
        if (static_cast<size_t>(m_file.tellp()) >= m_maxFileSize) {
            needsRotation = true;
        }
    }

    if (!needsRotation && m_file.is_open()) return;

    // Close current file
    if (m_file.is_open()) {
        m_file.close();
    }

    // Generate new filename with date
    std::wstringstream filename;
    filename << m_logDir << L"\\virtual-overlay-"
             << std::setfill(L'0') << std::setw(4) << (1900 + localTime.tm_year)
             << std::setw(2) << (localTime.tm_mon + 1)
             << std::setw(2) << localTime.tm_mday
             << L".log";

    m_currentLogPath = filename.str();

    // Open new file (append mode)
    m_file.open(m_currentLogPath, std::ios::app);

    // Clean up old log files
    try {
        std::vector<std::filesystem::path> logFiles;
        for (const auto& entry : std::filesystem::directory_iterator(m_logDir)) {
            if (entry.path().extension() == L".log" &&
                entry.path().filename().wstring().find(L"virtual-overlay-") == 0) {
                logFiles.push_back(entry.path());
            }
        }

        // Sort by modification time, oldest first
        std::sort(logFiles.begin(), logFiles.end(), 
            [](const auto& a, const auto& b) {
                return std::filesystem::last_write_time(a) < std::filesystem::last_write_time(b);
            });

        // Remove files older than max days
        while (logFiles.size() > static_cast<size_t>(m_maxDaysToKeep)) {
            std::filesystem::remove(logFiles.front());
            logFiles.erase(logFiles.begin());
        }
    } catch (...) {
        // Ignore cleanup errors
    }
}

std::string Logger::GetTimestamp() {
    time_t now = time(nullptr);
    tm localTime;
    localtime_s(&localTime, &now);

    // Get milliseconds
    SYSTEMTIME st;
    GetLocalTime(&st);

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << (1900 + localTime.tm_year) << "-"
        << std::setw(2) << (localTime.tm_mon + 1) << "-"
        << std::setw(2) << localTime.tm_mday << " "
        << std::setw(2) << localTime.tm_hour << ":"
        << std::setw(2) << localTime.tm_min << ":"
        << std::setw(2) << localTime.tm_sec << "."
        << std::setw(3) << st.wMilliseconds;

    return oss.str();
}

std::string Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        default:              return "?????";
    }
}

void Logger::EnsureLogDirectory() {
    try {
        std::filesystem::create_directories(m_logDir);
    } catch (...) {
        // Ignore - will fail to log if directory can't be created
    }
}

} // namespace VirtualOverlay
