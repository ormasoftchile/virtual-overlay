#pragma once

#include <windows.h>
#include <vector>
#include <string>

namespace VirtualOverlay {

struct MonitorInfo {
    HMONITOR handle;
    RECT bounds;
    RECT workArea;
    bool isPrimary;
    std::wstring name;
    UINT dpi;
};

class Monitor {
public:
    static Monitor& Instance();

    // Refresh monitor list
    void Refresh();

    // Get all monitors
    const std::vector<MonitorInfo>& GetMonitors() const;

    // Get monitor count
    size_t GetCount() const;

    // Get primary monitor
    const MonitorInfo* GetPrimary() const;

    // Get monitor at cursor position
    const MonitorInfo* GetAtCursor() const;

    // Get monitor at point
    const MonitorInfo* GetAtPoint(POINT pt) const;

    // Get monitor by handle
    const MonitorInfo* GetByHandle(HMONITOR handle) const;

    // Get monitor rect for a given handle
    RECT GetMonitorRect(HMONITOR handle) const;

    // Get current cursor position
    static POINT GetCursorPosition();

    // Get DPI for a monitor
    static UINT GetMonitorDpi(HMONITOR handle);

private:
    Monitor();
    ~Monitor() = default;
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    static BOOL CALLBACK EnumMonitorsCallback(HMONITOR hMonitor, HDC hdcMonitor, 
                                               LPRECT lprcMonitor, LPARAM dwData);

    std::vector<MonitorInfo> m_monitors;
};

}  // namespace VirtualOverlay
