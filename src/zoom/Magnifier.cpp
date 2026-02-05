#include "Magnifier.h"
#include "../utils/Logger.h"
#include <magnification.h>

#pragma comment(lib, "Magnification.lib")

namespace VirtualOverlay {

Magnifier& Magnifier::Instance() {
    static Magnifier instance;
    return instance;
}

Magnifier::Magnifier() = default;

Magnifier::~Magnifier() {
    if (m_initialized) {
        Shutdown();
    }
}

bool Magnifier::Init() {
    if (m_initialized) {
        return true;
    }

    // Check if Windows Magnifier is already running
    if (IsWindowsMagnifierActive()) {
        LOG_WARN("Windows Magnifier is already running, zoom may conflict");
    }

    // Initialize the Magnification API
    if (!MagInitialize()) {
        DWORD error = GetLastError();
        LOG_ERROR("MagInitialize failed with error: %lu", error);
        return false;
    }

    // Reset any stale magnification from a previous crash or abnormal exit
    // The Magnification API state persists at the system level
    if (!MagSetFullscreenTransform(1.0f, 0, 0)) {
        LOG_WARN("Failed to reset stale magnification on startup");
    }

    m_initialized = true;
    m_currentLevel = 1.0f;
    LOG_INFO("Magnification API initialized");
    return true;
}

void Magnifier::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Reset magnification before uninitializing
    ResetMagnification();

    // Uninitialize the Magnification API
    if (!MagUninitialize()) {
        DWORD error = GetLastError();
        LOG_WARN("MagUninitialize failed with error: %lu", error);
    }

    m_initialized = false;
    m_currentLevel = 1.0f;
    LOG_INFO("Magnification API shutdown");
}

bool Magnifier::IsInitialized() const {
    return m_initialized;
}

bool Magnifier::SetFullscreenMagnification(float level, int centerX, int centerY) {
    if (!m_initialized) {
        LOG_ERROR("Magnifier not initialized");
        return false;
    }

    // Clamp level to valid range
    if (level < 1.0f) level = 1.0f;
    if (level > 20.0f) level = 20.0f;

    // Calculate the offset for the magnification
    // The offset determines which part of the screen is visible
    // When zoomed, the visible area is (screenSize / level)
    // We want to center on (centerX, centerY)
    
    // Get virtual screen dimensions (spans all monitors)
    int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    
    // Calculate visible dimensions when zoomed
    float visibleWidth = static_cast<float>(virtualWidth) / level;
    float visibleHeight = static_cast<float>(virtualHeight) / level;
    
    // Calculate offset to center on the given point
    // The offset is relative to virtual screen origin
    int offsetX = static_cast<int>(centerX - virtualLeft - visibleWidth / 2.0f);
    int offsetY = static_cast<int>(centerY - virtualTop - visibleHeight / 2.0f);
    
    // Clamp offsets to valid range
    int maxOffsetX = static_cast<int>(virtualWidth - visibleWidth);
    int maxOffsetY = static_cast<int>(virtualHeight - visibleHeight);
    
    if (offsetX < 0) offsetX = 0;
    if (offsetY < 0) offsetY = 0;
    if (maxOffsetX > 0 && offsetX > maxOffsetX) offsetX = maxOffsetX;
    if (maxOffsetY > 0 && offsetY > maxOffsetY) offsetY = maxOffsetY;

    // Apply the magnification transform
    if (!MagSetFullscreenTransform(level, offsetX, offsetY)) {
        DWORD error = GetLastError();
        LOG_ERROR("MagSetFullscreenTransform failed with error: %lu", error);
        return false;
    }

    m_currentLevel = level;
    return true;
}

float Magnifier::GetMagnificationLevel() const {
    return m_currentLevel;
}

bool Magnifier::ResetMagnification() {
    if (!m_initialized) {
        return true;  // Nothing to reset
    }

    // Reset to 1.0x magnification at origin
    if (!MagSetFullscreenTransform(1.0f, 0, 0)) {
        DWORD error = GetLastError();
        LOG_WARN("MagSetFullscreenTransform(1.0) failed with error: %lu", error);
        return false;
    }

    m_currentLevel = 1.0f;
    LOG_DEBUG("Magnification reset to 1.0x");
    return true;
}

bool Magnifier::IsWindowsMagnifierActive() {
    // Check if the Windows Magnifier application is running
    HWND magWindow = FindWindowW(L"Screen Magnifier Window", nullptr);
    if (magWindow != nullptr) {
        return true;
    }

    // Also check for the newer magnifier lens window
    magWindow = FindWindowW(L"MagUIClass", nullptr);
    return magWindow != nullptr;
}

}  // namespace VirtualOverlay
