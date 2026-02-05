#include "ZoomController.h"
#include "Magnifier.h"
#include "../utils/Logger.h"
#include "../utils/Monitor.h"
#include <cmath>

namespace VirtualOverlay {

ZoomController& ZoomController::Instance() {
    static ZoomController instance;
    return instance;
}

ZoomController::ZoomController() = default;

ZoomController::~ZoomController() {
    if (m_initialized) {
        Shutdown();
    }
}

bool ZoomController::Init(const ZoomSettings& config) {
    if (m_initialized) {
        return true;
    }

    m_config = config;
    m_state.reset();
    m_controllerState = ZoomControllerState::Normal;

    // Initialize smooth values
    m_smoothLevel.SetSmoothing(config.smoothing ? config.smoothingFactor : 0.0f);
    m_smoothLevel.SetImmediate(1.0f);

    m_smoothOffsetX.SetSmoothing(config.smoothing ? config.smoothingFactor : 0.0f);
    m_smoothOffsetX.SetImmediate(0.0f);

    m_smoothOffsetY.SetSmoothing(config.smoothing ? config.smoothingFactor : 0.0f);
    m_smoothOffsetY.SetImmediate(0.0f);

    // Initialize magnifier
    if (!Magnifier::Instance().Init()) {
        LOG_ERROR("Failed to initialize Magnifier for ZoomController");
        return false;
    }

    m_initialized = true;
    LOG_INFO("ZoomController initialized");
    return true;
}

void ZoomController::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Reset zoom before shutdown
    ResetZoom();
    
    // Shutdown magnifier
    Magnifier::Instance().Shutdown();

    m_initialized = false;
    LOG_INFO("ZoomController shutdown");
}

void ZoomController::Update(float deltaTimeMs) {
    if (!m_initialized) return;

    float deltaTimeSec = deltaTimeMs / 1000.0f;

    // Update smooth values
    m_smoothLevel.Update(deltaTimeSec);
    m_smoothOffsetX.Update(deltaTimeSec);
    m_smoothOffsetY.Update(deltaTimeSec);

    // Update state
    m_state.currentLevel = m_smoothLevel.GetValue();
    m_state.offsetX = m_smoothOffsetX.GetValue();
    m_state.offsetY = m_smoothOffsetY.GetValue();

    // Update controller state
    if (m_state.currentLevel > 1.001f) {
        if (m_controllerState != ZoomControllerState::Zooming) {
            m_controllerState = ZoomControllerState::Zooming;
            LOG_DEBUG("Zoom state: Zooming");
        }
    } else if (m_smoothLevel.HasReachedTarget()) {
        if (m_controllerState != ZoomControllerState::Normal) {
            m_controllerState = ZoomControllerState::Normal;
            LOG_DEBUG("Zoom state: Normal");
        }
    }

    // Apply magnification if changed
    ApplyMagnification();
}

void ZoomController::ZoomIn() {
    if (!m_initialized) return;

    float newLevel = m_state.targetLevel + m_config.zoomStep;
    if (newLevel > m_config.maxZoom) {
        newLevel = m_config.maxZoom;
    }

    ZoomToLevel(newLevel);
}

void ZoomController::ZoomOut() {
    if (!m_initialized) return;

    float newLevel = m_state.targetLevel - m_config.zoomStep;
    if (newLevel < m_config.minZoom) {
        newLevel = m_config.minZoom;
    }

    ZoomToLevel(newLevel);
}

void ZoomController::ZoomToLevel(float level) {
    if (!m_initialized) return;

    // Clamp level
    if (level < m_config.minZoom) level = m_config.minZoom;
    if (level > m_config.maxZoom) level = m_config.maxZoom;

    m_state.targetLevel = level;
    m_smoothLevel.SetTarget(level);

    LOG_DEBUG("Zoom target set to %.2f", level);

    // If starting zoom, capture the active monitor
    if (m_state.activeMonitor == nullptr && level > 1.0f) {
        POINT pt = Monitor::GetCursorPosition();
        m_state.activeMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        
        // Initialize pan to cursor position
        UpdatePanFromCursor(pt.x, pt.y);
    }
}

void ZoomController::ResetZoom() {
    if (!m_initialized) return;

    m_state.targetLevel = 1.0f;
    m_state.targetOffsetX = 0.0f;
    m_state.targetOffsetY = 0.0f;

    m_smoothLevel.SetTarget(1.0f);
    m_smoothOffsetX.SetTarget(0.0f);
    m_smoothOffsetY.SetTarget(0.0f);

    m_state.activeMonitor = nullptr;

    LOG_DEBUG("Zoom reset");
}

void ZoomController::OnCursorMove(int x, int y) {
    if (!m_initialized) return;
    if (!m_state.isZoomed()) return;

    m_lastCursorX = x;
    m_lastCursorY = y;

    UpdatePanFromCursor(x, y);
}

void ZoomController::OnModifierPressed() {
    if (!m_initialized) return;

    if (!m_state.modifierHeld) {
        // Check for double-tap
        if (CheckDoubleTap()) {
            ResetZoom();
            m_state.lastModifierTap = 0;
            return;
        }

        m_state.lastModifierTap = GetTickCount();
    }

    m_state.modifierHeld = true;
}

void ZoomController::OnModifierReleased() {
    if (!m_initialized) return;
    m_state.modifierHeld = false;
}

ZoomControllerState ZoomController::GetState() const {
    return m_controllerState;
}

float ZoomController::GetCurrentLevel() const {
    return m_state.currentLevel;
}

float ZoomController::GetTargetLevel() const {
    return m_state.targetLevel;
}

bool ZoomController::IsZoomed() const {
    return m_state.isZoomed();
}

void ZoomController::ApplyConfig(const ZoomSettings& config) {
    m_config = config;

    float smoothing = config.smoothing ? config.smoothingFactor : 0.0f;
    m_smoothLevel.SetSmoothing(smoothing);
    m_smoothOffsetX.SetSmoothing(smoothing);
    m_smoothOffsetY.SetSmoothing(smoothing);

    LOG_INFO("ZoomController config updated");
}

void ZoomController::UpdatePanFromCursor(int cursorX, int cursorY) {
    if (m_state.activeMonitor == nullptr) return;

    // Get monitor bounds
    RECT monitorRect = Monitor::Instance().GetMonitorRect(m_state.activeMonitor);
    int monitorWidth = monitorRect.right - monitorRect.left;
    int monitorHeight = monitorRect.bottom - monitorRect.top;

    if (monitorWidth == 0 || monitorHeight == 0) return;

    // Normalize cursor position within monitor (0.0 to 1.0)
    float normX = static_cast<float>(cursorX - monitorRect.left) / monitorWidth;
    float normY = static_cast<float>(cursorY - monitorRect.top) / monitorHeight;

    // Clamp to valid range
    normX = std::clamp(normX, 0.0f, 1.0f);
    normY = std::clamp(normY, 0.0f, 1.0f);

    // Set target pan position
    m_state.targetOffsetX = normX;
    m_state.targetOffsetY = normY;
    
    m_smoothOffsetX.SetTarget(normX);
    m_smoothOffsetY.SetTarget(normY);
}

void ZoomController::ApplyMagnification() {
    // Skip magnification API calls when at or very close to 1.0x zoom
    // This prevents unnecessary API calls that can affect cursor behavior
    if (m_state.currentLevel <= 1.001f) {
        // If we were zoomed before and have now returned to 1.0, do a final reset
        if (m_state.activeMonitor != nullptr) {
            Magnifier::Instance().ResetMagnification();
            m_state.activeMonitor = nullptr;
        }
        return;
    }

    // Get monitor bounds for the active monitor or primary
    RECT monitorRect;
    if (m_state.activeMonitor) {
        monitorRect = Monitor::Instance().GetMonitorRect(m_state.activeMonitor);
    } else {
        const auto* primary = Monitor::Instance().GetPrimary();
        if (primary) {
            monitorRect = primary->bounds;
        } else {
            monitorRect = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        }
    }

    int monitorWidth = monitorRect.right - monitorRect.left;
    int monitorHeight = monitorRect.bottom - monitorRect.top;

    // Calculate center point based on normalized offset
    int centerX = monitorRect.left + static_cast<int>(m_state.offsetX * monitorWidth);
    int centerY = monitorRect.top + static_cast<int>(m_state.offsetY * monitorHeight);

    // Apply magnification
    Magnifier::Instance().SetFullscreenMagnification(m_state.currentLevel, centerX, centerY);
}

bool ZoomController::CheckDoubleTap() {
    if (!m_config.doubleTapToReset) return false;
    if (m_state.lastModifierTap == 0) return false;

    DWORD now = GetTickCount();
    DWORD elapsed = now - m_state.lastModifierTap;

    return elapsed <= static_cast<DWORD>(m_config.doubleTapWindowMs);
}

}  // namespace VirtualOverlay
