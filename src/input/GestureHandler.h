#pragma once

#include <windows.h>

namespace VirtualOverlay {

// Custom messages for gesture events
constexpr UINT WM_USER_PINCH_ZOOM = WM_USER + 110;

// Handles touchpad/touchscreen gestures
// Uses WM_GESTURE for pinch-to-zoom support
class GestureHandler {
public:
    static GestureHandler& Instance();

    // Initialize gesture handling for a window
    bool Init(HWND hwnd);

    // Shutdown gesture handling
    void Shutdown();

    // Process gesture messages
    // Call this from window procedure for WM_GESTURE messages
    // Returns true if the gesture was handled
    bool ProcessGesture(HWND hwnd, WPARAM wParam, LPARAM lParam);

    // Enable/disable gesture handling
    void SetEnabled(bool enabled);
    bool IsEnabled() const;

private:
    GestureHandler();
    ~GestureHandler();
    GestureHandler(const GestureHandler&) = delete;
    GestureHandler& operator=(const GestureHandler&) = delete;

    bool HandleZoomGesture(HWND hwnd, const GESTUREINFO& gi);

    HWND m_hwnd = nullptr;
    bool m_enabled = true;
    bool m_initialized = false;
    
    // Pinch gesture state
    DWORD m_gestureArgument = 0;  // Initial distance for pinch
    float m_baseZoomLevel = 1.0f;  // Zoom level when gesture started
};

}  // namespace VirtualOverlay
