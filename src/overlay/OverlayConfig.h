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

// Overlay visual style settings
struct OverlayStyleSettings {
    int padding = 16;                   // pixels
};

// Overlay text settings
struct OverlayTextSettings {
    std::wstring fontFamily = L"Segoe UI Variable";
    int fontWeight = 600;               // 100-900
};

// Complete overlay configuration
struct OverlaySettings {
    bool enabled = true;
    OverlayPosition position = OverlayPosition::TopCenter;
    MonitorSelection monitor = MonitorSelection::Cursor;
    
    bool showDesktopNumber = true;
    bool showDesktopName = true;
    std::wstring format = L"{number}: {name}";
    
    // Watermark appearance
    int watermarkFontSize = 72;
    float watermarkOpacity = 0.25f;
    bool watermarkShadow = true;
    uint32_t watermarkColor = 0xFFFFFF;  // White by default
    
    // Dodge mode - move overlay when mouse approaches
    bool dodgeOnHover = false;
    int dodgeProximity = 100;  // pixels
    
    OverlayStyleSettings style;
    OverlayTextSettings text;
};

// Overlay state machine states
enum class OverlayState {
    Hidden,
    Visible
};

// Runtime overlay state
struct OverlayRuntimeState {
    OverlayState state = OverlayState::Hidden;
    
    int currentDesktopIndex = 0;        // 1-based
    std::wstring currentDesktopName;
    
};

}  // namespace VirtualOverlay
