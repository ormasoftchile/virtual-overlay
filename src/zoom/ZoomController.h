#pragma once

#include "ZoomConfig.h"
#include "../utils/Animation.h"
#include <windows.h>

namespace VirtualOverlay {

// Zoom state machine states
enum class ZoomControllerState {
    Normal,     // No zoom active (level = 1.0)
    Zooming     // Actively zoomed (level > 1.0)
};

// Zoom controller manages the zoom state machine and coordinates
// with Magnifier, hooks, and cursor tracking
class ZoomController {
public:
    static ZoomController& Instance();

    // Initialize the controller with configuration
    bool Init(const ZoomSettings& config);

    // Shutdown the controller
    void Shutdown();

    // Update zoom animation and cursor tracking
    // Call this from timer or message loop
    void Update(float deltaTimeMs);

    // Handle zoom in/out requests from input
    void ZoomIn();
    void ZoomOut();
    void ZoomToLevel(float level);
    void ResetZoom();

    // Handle cursor movement for pan
    void OnCursorMove(int x, int y);

    // Handle modifier key state
    void OnModifierPressed();
    void OnModifierReleased();

    // Get current state
    ZoomControllerState GetState() const;
    float GetCurrentLevel() const;
    float GetTargetLevel() const;
    bool IsZoomed() const;

    // Apply new configuration
    void ApplyConfig(const ZoomSettings& config);

private:
    ZoomController();
    ~ZoomController();
    ZoomController(const ZoomController&) = delete;
    ZoomController& operator=(const ZoomController&) = delete;

    void UpdatePanFromCursor(int cursorX, int cursorY);
    void ApplyMagnification();
    bool CheckDoubleTap();

    ZoomSettings m_config;
    ZoomState m_state;
    ZoomControllerState m_controllerState = ZoomControllerState::Normal;

    // Smooth animation values
    SmoothValue m_smoothLevel;
    SmoothValue m_smoothOffsetX;
    SmoothValue m_smoothOffsetY;

    // Last cursor position
    int m_lastCursorX = 0;
    int m_lastCursorY = 0;

    bool m_initialized = false;
};

}  // namespace VirtualOverlay
