#include "Monitor.h"
#include <shellscalingapi.h>

#pragma comment(lib, "shcore.lib")

namespace VirtualOverlay {

Monitor& Monitor::Instance() {
    static Monitor instance;
    return instance;
}

Monitor::Monitor() {
    Refresh();
}

void Monitor::Refresh() {
    m_monitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsCallback, 
                        reinterpret_cast<LPARAM>(this));
}

BOOL CALLBACK Monitor::EnumMonitorsCallback(HMONITOR hMonitor, HDC /*hdcMonitor*/,
                                             LPRECT /*lprcMonitor*/, LPARAM dwData) {
    Monitor* self = reinterpret_cast<Monitor*>(dwData);
    
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMonitor, &mi)) {
        return TRUE;  // Continue enumeration
    }

    MonitorInfo info = {};
    info.handle = hMonitor;
    info.bounds = mi.rcMonitor;
    info.workArea = mi.rcWork;
    info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    info.name = mi.szDevice;
    info.dpi = GetMonitorDpi(hMonitor);

    self->m_monitors.push_back(info);
    return TRUE;
}

const std::vector<MonitorInfo>& Monitor::GetMonitors() const {
    return m_monitors;
}

size_t Monitor::GetCount() const {
    return m_monitors.size();
}

const MonitorInfo* Monitor::GetPrimary() const {
    for (const auto& mon : m_monitors) {
        if (mon.isPrimary) {
            return &mon;
        }
    }
    return m_monitors.empty() ? nullptr : &m_monitors[0];
}

const MonitorInfo* Monitor::GetAtCursor() const {
    POINT pt = GetCursorPosition();
    return GetAtPoint(pt);
}

const MonitorInfo* Monitor::GetAtPoint(POINT pt) const {
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    return GetByHandle(hMon);
}

const MonitorInfo* Monitor::GetByHandle(HMONITOR handle) const {
    for (const auto& mon : m_monitors) {
        if (mon.handle == handle) {
            return &mon;
        }
    }
    return nullptr;
}

RECT Monitor::GetMonitorRect(HMONITOR handle) const {
    const MonitorInfo* info = GetByHandle(handle);
    if (info) {
        return info->bounds;
    }
    
    // Fallback: query directly
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(handle, &mi)) {
        return mi.rcMonitor;
    }
    
    return {};
}

POINT Monitor::GetCursorPosition() {
    POINT pt = {};
    GetCursorPos(&pt);
    return pt;
}

UINT Monitor::GetMonitorDpi(HMONITOR handle) {
    UINT dpiX = 96, dpiY = 96;
    
    // Try GetDpiForMonitor (Windows 8.1+)
    HRESULT hr = GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    if (SUCCEEDED(hr)) {
        return dpiX;
    }
    
    // Fallback to system DPI
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        dpiX = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
        ReleaseDC(nullptr, hdc);
    }
    
    return dpiX;
}

}  // namespace VirtualOverlay
