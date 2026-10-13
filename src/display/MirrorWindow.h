#pragma once

#include <windows.h>
#include <string>
#include <memory>

namespace VirtualOverlay {

// A full-monitor, click-through window that displays the monitor's own desktop
// content mirrored horizontally (teleprompter / beam-splitter flip).
//
// Uses Windows.Graphics.Capture + Direct2D / D3D11 for cross-GPU, high-DPI,
// negative-coordinate-safe desktop mirroring with zero lag. Falls back to
// the Magnification API if Windows.Graphics.Capture is not supported.
class MirrorWindow {
public:
    MirrorWindow();
    ~MirrorWindow();

    MirrorWindow(const MirrorWindow&) = delete;
    MirrorWindow& operator=(const MirrorWindow&) = delete;

    // Create and show the mirror covering the given monitor bounds.
    // deviceName is the stable monitor device name (e.g. "\\.\DISPLAY2").
    bool Create(HINSTANCE hInstance, const std::wstring& deviceName, const RECT& bounds, HMONITOR hMonitor);

    // Reposition/resize to new monitor bounds.
    void UpdateBounds(const RECT& bounds, HMONITOR hMonitor);

    // Toggle whether the mirrored system cursor is drawn with the content.
    void SetMagnifiedCursor(bool enabled);

    // Bring the mirror back above other topmost windows (menus, tooltips)
    // that appeared on top of it.
    void RaiseToTop();

    void Destroy();

    bool IsValid() const { return m_hwnd != nullptr; }
    const std::wstring& DeviceName() const { return m_deviceName; }
    const RECT& Bounds() const { return m_bounds; }
    HMONITOR MonitorHandle() const { return m_hMonitor; }
    HWND GetHwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void EnsureClassRegistered(HINSTANCE hInstance);

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;
    std::wstring m_deviceName;
    RECT m_bounds = {};
    HMONITOR m_hMonitor = nullptr;
    bool m_magnifiedCursor = true;

    static constexpr wchar_t WINDOW_CLASS[] = L"VirtualOverlayMirrorWindow";
};

}  // namespace VirtualOverlay
