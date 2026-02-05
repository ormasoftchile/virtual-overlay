#pragma once

#include <string>

namespace VirtualOverlay {

// Default configuration values from data-model.md
namespace Defaults {

    // General Settings
    constexpr bool StartWithWindows = false;  // Default off for safety
    constexpr bool ShowTrayIcon = true;
    inline const char* SettingsHotkey = "Ctrl+Shift+O";

    // Zoom Settings
    constexpr bool ZoomEnabled = true;
    inline const char* ZoomModifierKey = "ctrl";
    constexpr float ZoomStep = 0.5f;           // Increased for faster zoom
    constexpr float MinZoom = 1.0f;
    constexpr float MaxZoom = 10.0f;
    constexpr bool ZoomSmoothing = true;
    constexpr float SmoothingFactor = 0.08f;   // Reduced for snappier response
    constexpr int AnimationDurationMs = 50;    // Reduced for faster animation
    constexpr bool DoubleTapToReset = true;
    constexpr int DoubleTapWindowMs = 300;
    constexpr bool TouchpadPinch = true;

    // Overlay Settings
    constexpr bool OverlayEnabled = true;
    inline const char* OverlayPosition = "top-center";  // top-left, top-center, top-right, center, bottom-left, bottom-center, bottom-right
    constexpr bool ShowDesktopNumber = true;
    constexpr bool ShowDesktopName = true;
    inline const char* OverlayFormat = "{name}";
    constexpr bool AutoHide = true;
    constexpr int AutoHideDelayMs = 2000;
    inline const char* OverlayMonitor = "cursor";  // cursor, primary, all

    // Style Settings
    inline const char* BlurStyle = "acrylic";  // acrylic, mica, solid
    inline const char* TintColor = "#000000";
    constexpr float TintOpacity = 0.6f;
    constexpr int CornerRadius = 8;
    inline const char* BorderColor = "#404040";
    constexpr int BorderWidth = 1;
    constexpr bool ShadowEnabled = true;
    constexpr int Padding = 16;

    // Text Settings
    inline const char* FontFamily = "Segoe UI Variable";
    constexpr int FontSize = 20;
    constexpr int FontWeight = 600;
    inline const char* TextColor = "#FFFFFF";

    // Animation Settings
    constexpr int FadeInDurationMs = 150;
    constexpr int FadeOutDurationMs = 200;
    constexpr bool SlideIn = true;
    constexpr int SlideDistance = 10;

    // Logging
    constexpr int MaxLogDays = 7;
    constexpr size_t MaxLogFileSize = 10 * 1024 * 1024;  // 10 MB

    // App info
    inline const char* AppName = "Virtual Overlay";
    inline const char* AppVersion = "1.0.0";
    inline const wchar_t* ConfigFileName = L"config.json";
    inline const wchar_t* LogDirName = L"logs";

}  // namespace Defaults

}  // namespace VirtualOverlay
