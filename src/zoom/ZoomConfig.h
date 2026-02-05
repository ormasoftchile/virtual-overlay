#pragma once

#include <windows.h>

namespace VirtualOverlay {

// Zoom feature configuration (runtime settings for zoom subsystem)
struct ZoomSettings {
    bool enabled = true;
    
    // Which modifier key triggers zoom (VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN)
    UINT modifierVirtualKey = VK_CONTROL;
    
    // Zoom behavior
    float zoomStep = 0.5f;           // Amount to zoom per scroll notch
    float minZoom = 1.0f;            // Minimum zoom (1.0 = no zoom)
    float maxZoom = 10.0f;           // Maximum zoom level
    
    // Smoothing
    bool smoothing = true;           // Enable smooth pan/zoom transitions
    float smoothingFactor = 0.08f;   // Lower = smoother but slower (0.05–0.5)
    int animationDurationMs = 50;    // Zoom transition duration
    
    // Double-tap reset
    bool doubleTapToReset = true;    // Double-tap modifier resets zoom
    int doubleTapWindowMs = 300;     // Time window for double-tap detection
    
    // Touchpad support
    bool touchpadPinch = true;       // Enable pinch gesture support
};

// Runtime state for zoom feature
struct ZoomState {
    float currentLevel = 1.0f;       // Current zoom level (1.0 = no zoom)
    float targetLevel = 1.0f;        // Target zoom level for animation
    
    float offsetX = 0.0f;            // Pan offset X (normalized 0-1)
    float offsetY = 0.0f;            // Pan offset Y (normalized 0-1)
    float targetOffsetX = 0.0f;      // Target pan offset X
    float targetOffsetY = 0.0f;      // Target pan offset Y
    
    HMONITOR activeMonitor = nullptr; // Monitor being zoomed
    
    bool modifierHeld = false;       // Is modifier key currently pressed
    DWORD lastModifierTap = 0;       // Timestamp for double-tap detection
    
    bool isZoomed() const { return currentLevel > 1.0f; }
    void reset() {
        currentLevel = 1.0f;
        targetLevel = 1.0f;
        offsetX = 0.0f;
        offsetY = 0.0f;
        targetOffsetX = 0.0f;
        targetOffsetY = 0.0f;
        activeMonitor = nullptr;
    }
};

}  // namespace VirtualOverlay
