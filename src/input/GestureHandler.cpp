#include "GestureHandler.h"
#include "../utils/Logger.h"
#include "../zoom/ZoomController.h"

namespace VirtualOverlay {

GestureHandler& GestureHandler::Instance() {
    static GestureHandler instance;
    return instance;
}

GestureHandler::GestureHandler() = default;

GestureHandler::~GestureHandler() {
    if (m_initialized) {
        Shutdown();
    }
}

bool GestureHandler::Init(HWND hwnd) {
    if (m_initialized) {
        return true;
    }

    m_hwnd = hwnd;

    // Configure which gestures we want to receive
    GESTURECONFIG gc = {};
    gc.dwID = GID_ZOOM;
    gc.dwWant = GC_ZOOM;
    gc.dwBlock = 0;

    if (!SetGestureConfig(hwnd, 0, 1, &gc, sizeof(GESTURECONFIG))) {
        DWORD error = GetLastError();
        LOG_WARN("SetGestureConfig failed with error: %lu - gestures may not work", error);
        // Don't fail initialization - gestures are optional
    }

    m_initialized = true;
    LOG_INFO("GestureHandler initialized");
    return true;
}

void GestureHandler::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_hwnd = nullptr;
    m_initialized = false;
    LOG_INFO("GestureHandler shutdown");
}

bool GestureHandler::ProcessGesture(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    if (!m_enabled || !m_initialized) {
        return false;
    }

    GESTUREINFO gi = {};
    gi.cbSize = sizeof(GESTUREINFO);

    if (!GetGestureInfo(reinterpret_cast<HGESTUREINFO>(lParam), &gi)) {
        DWORD error = GetLastError();
        LOG_WARN("GetGestureInfo failed with error: %lu", error);
        return false;
    }

    bool handled = false;

    switch (gi.dwID) {
        case GID_ZOOM:
            handled = HandleZoomGesture(hwnd, gi);
            break;

        case GID_BEGIN:
            // Gesture beginning
            m_gestureArgument = 0;
            m_baseZoomLevel = ZoomController::Instance().GetCurrentLevel();
            break;

        case GID_END:
            // Gesture ending
            m_gestureArgument = 0;
            break;

        default:
            break;
    }

    // Close the gesture info handle
    CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lParam));

    return handled;
}

void GestureHandler::SetEnabled(bool enabled) {
    m_enabled = enabled;
}

bool GestureHandler::IsEnabled() const {
    return m_enabled;
}

bool GestureHandler::HandleZoomGesture(HWND hwnd, const GESTUREINFO& gi) {
    (void)hwnd;  // Unused

    if (gi.dwFlags & GF_BEGIN) {
        // Store the initial distance
        m_gestureArgument = gi.ullArguments;
        m_baseZoomLevel = ZoomController::Instance().GetCurrentLevel();
        return true;
    }

    if (m_gestureArgument == 0) {
        return false;
    }

    // Calculate zoom factor from pinch gesture
    // ullArguments contains the distance between fingers
    DWORD currentDistance = static_cast<DWORD>(gi.ullArguments);
    float scaleFactor = static_cast<float>(currentDistance) / static_cast<float>(m_gestureArgument);

    // Apply to base zoom level
    float newLevel = m_baseZoomLevel * scaleFactor;

    // Set the zoom level
    ZoomController::Instance().ZoomToLevel(newLevel);

    LOG_DEBUG("Pinch gesture: scale=%.2f, level=%.2f", scaleFactor, newLevel);

    return true;
}

}  // namespace VirtualOverlay
