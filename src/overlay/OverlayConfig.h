#pragma once

#include <windows.h>
#include <string>
#include <cstdint>
#include "../config/Config.h"

namespace VirtualOverlay {

// Use types from Config.h to avoid redefinition
// Aliases for convenience within overlay module
using OverlayPositionType = OverlayPosition;  // Already defined in Config.h
using OverlayMonitorType = MonitorSelection;  // Alias
using BackdropStyleType = BlurType;           // Alias

// Overlay visual style settings
struct OverlayStyleSettings {
    BlurType backdrop = BlurType::Acrylic;
    uint32_t tintColor = 0x000000;      // RGB
    float tintOpacity = 0.6f;           // 0.0-1.0
    int cornerRadius = 8;               // pixels
    uint32_t borderColor = 0x404040;    // RGB
    int borderWidth = 1;                // pixels
    bool shadowEnabled = true;
    int padding = 16;                   // pixels
};

// Overlay text settings
struct OverlayTextSettings {
    std::wstring fontFamily = L"Segoe UI Variable";
    int fontSize = 20;                  // points
    int fontWeight = 600;               // 100-900
    uint32_t color = 0xFFFFFF;          // RGB
};

// Overlay animation settings
struct OverlayAnimationSettings {
    int fadeInDurationMs = 150;
    int fadeOutDurationMs = 200;
    bool slideIn = true;
    int slideDistance = 10;             // pixels
};

// Complete overlay configuration
struct OverlaySettings {
    bool enabled = true;
    OverlayMode mode = OverlayMode::Notification;
    OverlayPosition position = OverlayPosition::TopCenter;
    MonitorSelection monitor = MonitorSelection::Cursor;
    
    bool showDesktopNumber = true;
    bool showDesktopName = true;
    std::wstring format = L"{number}: {name}";
    
    bool autoHide = true;
    int autoHideDelayMs = 2000;
    
    // Watermark mode settings
    int watermarkFontSize = 72;
    float watermarkOpacity = 0.25f;
    bool watermarkShadow = true;
    uint32_t watermarkColor = 0xFFFFFF;  // White by default
    
    // Dodge mode - move overlay when mouse approaches
    bool dodgeOnHover = false;
    int dodgeProximity = 100;  // pixels
    
    OverlayStyleSettings style;
    OverlayTextSettings text;
    OverlayAnimationSettings animation;
};

// Overlay state machine states
enum class OverlayState {
    Hidden,
    FadeIn,
    Visible,
    FadeOut
};

// Runtime overlay state
struct OverlayRuntimeState {
    OverlayState state = OverlayState::Hidden;
    float opacity = 0.0f;               // Current opacity for animation
    float slideOffset = 0.0f;           // Current slide offset
    
    int currentDesktopIndex = 0;        // 1-based
    std::wstring currentDesktopName;
    
    DWORD stateStartTime = 0;           // When current state started
    DWORD visibleStartTime = 0;         // When became visible (for auto-hide)
};

}  // namespace VirtualOverlay
