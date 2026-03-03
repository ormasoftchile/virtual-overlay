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

    // Don't call MagInitialize here - defer it to SetFullscreenMagnification.
    // The DWM magnification pipeline adds mouse input latency even at 1.0x,
    // so we only activate it when actually zooming.

    // Reset any stale magnification from a previous crash or abnormal exit
    if (MagInitialize()) {
        MagSetFullscreenTransform(1.0f, 0, 0);
        MagUninitialize();
    }

    m_currentLevel = 1.0f;
    LOG_INFO("Magnifier ready (API will activate on first zoom)");
    return true;
}

void Magnifier::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Reset and uninitialize (ResetMagnification handles both)
    ResetMagnification();

    LOG_INFO("Magnification API shutdown");
}

bool Magnifier::IsInitialized() const {
    return m_initialized;
}

bool Magnifier::SetFullscreenMagnification(float level, int centerX, int centerY) {
    // Lazily initialize the Magnification API on first use.
    // We defer initialization so the DWM magnification pipeline
    // is only active while actually zoomed, avoiding mouse latency.
    if (!m_initialized) {
        // Save mouse speed and acceleration before activating magnification.
        // MagSetFullscreenTransform without UIAccess can modify the DWM
        // cursor pipeline and fail to revert it on MagUninitialize.
        m_mouseSettingsSaved = false;
        if (SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &m_savedMouseSpeed, 0)) {
            if (SystemParametersInfoW(SPI_GETMOUSE, 0, m_savedMouseParams, 0)) {
                m_mouseSettingsSaved = true;
            }
        }

        if (!MagInitialize()) {
            DWORD error = GetLastError();
            LOG_ERROR("MagInitialize failed with error: %lu", error);
            return false;
        }
        m_initialized = true;
        LOG_DEBUG("Magnification API activated for zoom session");
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

    // Skip redundant API calls when values haven't changed
    if (level == m_lastLevel && offsetX == m_lastOffsetX && offsetY == m_lastOffsetY) {
        return true;
    }

    // Apply the magnification transform
    if (!MagSetFullscreenTransform(level, offsetX, offsetY)) {
        DWORD error = GetLastError();
        LOG_ERROR("MagSetFullscreenTransform failed with error: %lu", error);
        return false;
    }

    m_currentLevel = level;
    m_lastLevel = level;
    m_lastOffsetX = offsetX;
    m_lastOffsetY = offsetY;
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
    }

    // Restore cursor visibility before uninitializing
    MagShowSystemCursor(TRUE);

    // Fully uninitialize the Magnification API
    if (!MagUninitialize()) {
        DWORD error = GetLastError();
        LOG_WARN("MagUninitialize failed with error: %lu", error);
    }

    // Force-restore mouse speed and acceleration settings.
    // Without UIAccess, the DWM magnification pipeline can modify
    // cursor input behavior and not revert it on MagUninitialize.
    if (m_mouseSettingsSaved) {
        SystemParametersInfoW(SPI_SETMOUSESPEED, 0,
            reinterpret_cast<LPVOID>(static_cast<INT_PTR>(m_savedMouseSpeed)),
            SPIF_SENDCHANGE);
        SystemParametersInfoW(SPI_SETMOUSE, 0, m_savedMouseParams,
            SPIF_SENDCHANGE);
        m_mouseSettingsSaved = false;
        LOG_DEBUG("Mouse settings restored (speed=%d)", m_savedMouseSpeed);
    }

    m_initialized = false;
    m_currentLevel = 1.0f;
    m_lastLevel = 0.0f;
    m_lastOffsetX = -1;
    m_lastOffsetY = -1;
    LOG_DEBUG("Magnification fully deactivated");
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
