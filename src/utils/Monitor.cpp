#include "Monitor.h"
#include <shellscalingapi.h>
#include <algorithm>
#include <unordered_map>

#pragma comment(lib, "shcore.lib")

namespace VirtualOverlay {

std::wstring MonitorInfo::GetDisplayName(size_t /*index*/) const {
    std::wstring label;
    if (!friendlyName.empty()) {
        label = friendlyName + L" (";
    } else {
        label = name + L" (";
    }
    label += std::to_wstring(bounds.right - bounds.left) + L"x" +
             std::to_wstring(bounds.bottom - bounds.top);
    if (isPrimary) {
        label += L", Primary";
    }
    label += L")";
    return label;
}

static std::unordered_map<std::wstring, std::wstring> GetDisplayFriendlyNames() {
    std::unordered_map<std::wstring, std::wstring> names;

    UINT32 numPathArrayElements = 0;
    UINT32 numModeInfoArrayElements = 0;
    LONG status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPathArrayElements, &numModeInfoArrayElements);
    if (status != ERROR_SUCCESS || numPathArrayElements == 0) {
        return names;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> pathInfos(numPathArrayElements);
    std::vector<DISPLAYCONFIG_MODE_INFO> modeInfos(numModeInfoArrayElements);
    status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPathArrayElements, pathInfos.data(),
                                &numModeInfoArrayElements, modeInfos.data(), nullptr);
    if (status != ERROR_SUCCESS) {
        return names;
    }
    pathInfos.resize(numPathArrayElements);

    for (const auto& path : pathInfos) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(sourceName);
        sourceName.header.adapterId = path.sourceInfo.adapterId;
        sourceName.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS) {
            continue;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(targetName);
        targetName.header.adapterId = path.targetInfo.adapterId;
        targetName.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&targetName.header) != ERROR_SUCCESS) {
            continue;
        }

        if (targetName.monitorFriendlyDeviceName[0] != L'\0') {
            names[sourceName.viewGdiDeviceName] = targetName.monitorFriendlyDeviceName;
        }
    }
    return names;
}

static std::wstring GetFallbackDeviceString(const std::wstring& gdiName) {
    DISPLAY_DEVICEW ddMon = {};
    ddMon.cb = sizeof(ddMon);
    if (EnumDisplayDevicesW(gdiName.c_str(), 0, &ddMon, 0)) {
        if (ddMon.DeviceString[0] != L'\0' &&
            _wcsicmp(ddMon.DeviceString, L"Generic PnP Monitor") != 0 &&
            _wcsicmp(ddMon.DeviceString, L"Generic Non-PnP Monitor") != 0) {
            return ddMon.DeviceString;
        }
    }
    return L"";
}

struct MonitorEnumContext {
    Monitor* monitor;
    const std::unordered_map<std::wstring, std::wstring>* friendlyNames;
};

Monitor& Monitor::Instance() {
    static Monitor instance;
    return instance;
}

Monitor::Monitor() {
    Refresh();
}

void Monitor::Refresh() {
    m_monitors.clear();
    auto friendlyNames = GetDisplayFriendlyNames();
    MonitorEnumContext ctx = { this, &friendlyNames };
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsCallback, 
                        reinterpret_cast<LPARAM>(&ctx));

    // Ensure primary monitor is always first
    std::stable_sort(m_monitors.begin(), m_monitors.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        return a.isPrimary > b.isPrimary;
    });
}

BOOL CALLBACK Monitor::EnumMonitorsCallback(HMONITOR hMonitor, HDC /*hdcMonitor*/,
                                             LPRECT /*lprcMonitor*/, LPARAM dwData) {
    auto* ctx = reinterpret_cast<MonitorEnumContext*>(dwData);
    Monitor* self = ctx->monitor;
    
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

    auto it = ctx->friendlyNames->find(info.name);
    if (it != ctx->friendlyNames->end() && !it->second.empty()) {
        info.friendlyName = it->second;
    } else {
        info.friendlyName = GetFallbackDeviceString(info.name);
    }

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
    for (const auto& mon : m_monitors) {
        if (PtInRect(&mon.bounds, pt)) {
            return &mon;
        }
    }
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

const MonitorInfo* Monitor::GetByName(const std::wstring& name) const {
    for (const auto& mon : m_monitors) {
        if (_wcsicmp(mon.name.c_str(), name.c_str()) == 0) {
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
