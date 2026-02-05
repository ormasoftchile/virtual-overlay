#include "App.h"
#include "utils/Logger.h"
#include "utils/Monitor.h"
#include "config/Config.h"
#include "zoom/ZoomController.h"
#include "zoom/ZoomConfig.h"
#include "input/InputHandler.h"
#include "input/GestureHandler.h"
#include "overlay/OverlayWindow.h"
#include "overlay/OverlayConfig.h"
#include "desktop/VirtualDesktop.h"
#include "settings/SettingsWindow.h"
#include "tray/TrayIcon.h"
#include <algorithm>
#include <cwctype>

namespace VirtualOverlay {

// Helper to convert config modifier key to virtual key code
static UINT ModifierKeyToVK(ModifierKey key) {
    switch (key) {
        case ModifierKey::Ctrl:  return VK_CONTROL;
        case ModifierKey::Alt:   return VK_MENU;
        case ModifierKey::Shift: return VK_SHIFT;
        case ModifierKey::Win:   return VK_LWIN;
        default:                 return VK_CONTROL;
    }
}

// Helper to parse hotkey string like "Ctrl+Shift+D" into modifiers and virtual key
static bool ParseHotkeyString(const std::wstring& hotkey, UINT& modifiers, UINT& vk) {
    modifiers = 0;
    vk = 0;
    
    if (hotkey.empty()) return false;
    
    // Split by '+'
    std::wstring remaining = hotkey;
    std::wstring token;
    
    while (!remaining.empty()) {
        size_t pos = remaining.find(L'+');
        if (pos != std::wstring::npos) {
            token = remaining.substr(0, pos);
            remaining = remaining.substr(pos + 1);
        } else {
            token = remaining;
            remaining.clear();
        }
        
        // Trim whitespace
        while (!token.empty() && iswspace(token.front())) token.erase(0, 1);
        while (!token.empty() && iswspace(token.back())) token.pop_back();
        
        // Check for modifiers (case insensitive)
        std::wstring upper = token;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
        
        if (upper == L"CTRL" || upper == L"CONTROL") {
            modifiers |= MOD_CONTROL;
        } else if (upper == L"ALT") {
            modifiers |= MOD_ALT;
        } else if (upper == L"SHIFT") {
            modifiers |= MOD_SHIFT;
        } else if (upper == L"WIN" || upper == L"WINDOWS") {
            modifiers |= MOD_WIN;
        } else if (upper.length() == 1 && iswalpha(upper[0])) {
            // Single letter key
            vk = static_cast<UINT>(upper[0]);
        } else if (upper.length() == 1 && iswdigit(upper[0])) {
            // Digit key
            vk = static_cast<UINT>(upper[0]);
        }
    }
    
    return (modifiers != 0 && vk != 0);
}

App& App::Instance() {
    static App instance;
    return instance;
}

App::App() = default;

App::~App() {
    if (m_initialized) {
        Shutdown();
    }
}

bool App::Init(HINSTANCE hInstance, HWND hMainWnd) {
    if (m_initialized) {
        LOG_WARN("App::Init called when already initialized");
        return true;
    }

    LOG_INFO("Initializing application...");

    m_hInstance = hInstance;
    m_hMainWnd = hMainWnd;

    // Initialize monitors
    if (!InitMonitors()) {
        LOG_ERROR("Failed to initialize monitors");
        return false;
    }

    // Initialize zoom feature if enabled
    const auto& config = Config::Instance().Get();
    if (config.zoom.enabled) {
        if (InitZoom()) {
            m_zoomEnabled = true;
        } else {
            LOG_WARN("Failed to initialize zoom feature");
            // Continue anyway - zoom is optional
        }
    }

    // Initialize overlay feature if enabled
    if (config.overlay.enabled) {
        if (InitOverlay()) {
            m_overlayEnabled = true;
        } else {
            LOG_WARN("Failed to initialize overlay feature");
            // Continue anyway - overlay is optional
        }
    }

    // Initialize settings window
    InitSettings();

    // Initialize tray icon if enabled
    LOG_INFO("showTrayIcon config value: %s", config.general.showTrayIcon ? "true" : "false");
    if (config.general.showTrayIcon) {
        if (!InitTrayIcon()) {
            LOG_WARN("TrayIcon initialization returned false");
        }
    } else {
        LOG_INFO("Tray icon disabled in config");
    }

    // Register overlay toggle hotkey
    UINT mods = 0, vk = 0;
    if (ParseHotkeyString(config.general.overlayToggleHotkey, mods, vk)) {
        if (RegisterHotKey(m_hMainWnd, HOTKEY_OVERLAY_TOGGLE, mods | MOD_NOREPEAT, vk)) {
            LOG_INFO("Registered overlay toggle hotkey: %ws", config.general.overlayToggleHotkey.c_str());
        } else {
            LOG_WARN("Failed to register overlay toggle hotkey");
        }
    }

    m_initialized = true;
    m_running = true;
    m_lastUpdateTime = GetTickCount();

    LOG_INFO("Application initialized successfully");
    LOG_INFO("Detected %zu monitor(s)", Monitor::Instance().GetCount());
    LOG_INFO("Zoom enabled: %s", m_zoomEnabled ? "true" : "false");
    LOG_INFO("Overlay enabled: %s", m_overlayEnabled ? "true" : "false");

    return true;
}

void App::Run() {
    if (!m_initialized) {
        LOG_ERROR("App::Run called before initialization");
        return;
    }

    LOG_DEBUG("Application running");
    // Note: Message loop is handled in main.cpp
    // This method can be used for any per-frame updates if needed
}

void App::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down application...");
    m_running = false;

    // Unregister hotkeys
    if (m_hMainWnd) {
        UnregisterHotKey(m_hMainWnd, HOTKEY_OVERLAY_TOGGLE);
    }

    // Stop zoom update timer
    if (m_hMainWnd) {
        KillTimer(m_hMainWnd, TIMER_ZOOM_UPDATE);
    }

    // Shutdown overlay first (depends on VirtualDesktop)
    if (m_overlayEnabled) {
        VirtualDesktop::Instance().ClearDesktopSwitchCallback();
        OverlayWindow::Instance().Shutdown();
        VirtualDesktop::Instance().Shutdown();
        m_overlayEnabled = false;
    }

    // Shutdown settings window
    SettingsWindow::Instance().Shutdown();

    // Shutdown tray icon
    TrayIcon::Instance().Shutdown();

    // Shutdown zoom
    if (m_zoomEnabled) {
        InputHandler::Instance().Shutdown();
        GestureHandler::Instance().Shutdown();
        ZoomController::Instance().Shutdown();
        m_zoomEnabled = false;
    }

    // Future cleanup:
    // m_trayIcon.reset();

    m_initialized = false;
    LOG_INFO("Application shutdown complete");
}

bool App::InitMonitors() {
    // Monitor utility initializes itself on first access
    auto& monitor = Monitor::Instance();
    
    if (monitor.GetCount() == 0) {
        LOG_WARN("No monitors detected");
        // This shouldn't happen, but try to refresh
        monitor.Refresh();
    }

    const auto* primary = monitor.GetPrimary();
    if (primary) {
        LOG_DEBUG("Primary monitor: %dx%d at (%d, %d), DPI %u",
                  primary->bounds.right - primary->bounds.left,
                  primary->bounds.bottom - primary->bounds.top,
                  primary->bounds.left, primary->bounds.top,
                  primary->dpi);
    }

    return true;
}

bool App::InitZoom() {
    const auto& config = Config::Instance().Get();

    // Build ZoomSettings from app config
    ZoomSettings zoomSettings;
    zoomSettings.enabled = config.zoom.enabled;
    zoomSettings.modifierVirtualKey = ModifierKeyToVK(config.zoom.modifierKey);
    zoomSettings.zoomStep = config.zoom.zoomStep;
    zoomSettings.minZoom = config.zoom.minZoom;
    zoomSettings.maxZoom = config.zoom.maxZoom;
    zoomSettings.smoothing = config.zoom.smoothing;
    zoomSettings.smoothingFactor = config.zoom.smoothingFactor;
    zoomSettings.animationDurationMs = config.zoom.animationDurationMs;
    zoomSettings.doubleTapToReset = config.zoom.doubleTapToReset;
    zoomSettings.doubleTapWindowMs = config.zoom.doubleTapWindowMs;
    zoomSettings.touchpadPinch = config.zoom.touchpadPinch;

    // Initialize zoom controller
    if (!ZoomController::Instance().Init(zoomSettings)) {
        LOG_ERROR("Failed to initialize ZoomController");
        return false;
    }

    // Initialize input handler
    if (!InputHandler::Instance().Init(m_hMainWnd, zoomSettings.modifierVirtualKey)) {
        LOG_ERROR("Failed to initialize InputHandler");
        ZoomController::Instance().Shutdown();
        return false;
    }

    // Initialize gesture handler for touchpad pinch (optional)
    if (zoomSettings.touchpadPinch) {
        GestureHandler::Instance().Init(m_hMainWnd);
        // Don't fail if gestures don't work
    }

    // Start zoom update timer
    SetTimer(m_hMainWnd, TIMER_ZOOM_UPDATE, TIMER_ZOOM_INTERVAL_MS, nullptr);

    LOG_INFO("Zoom feature initialized");
    return true;
}

bool App::InitOverlay() {
    const auto& config = Config::Instance().Get();
    
    LOG_INFO("InitOverlay: config.overlay.position=%d", static_cast<int>(config.overlay.position));

    // Initialize VirtualDesktop COM interfaces
    if (!VirtualDesktop::Instance().Init()) {
        LOG_WARN("Failed to initialize VirtualDesktop - overlay will be limited");
        // Continue - we'll show overlay without desktop info
    }

    // Initialize overlay window
    if (!OverlayWindow::Instance().Init(m_hInstance)) {
        LOG_ERROR("Failed to initialize OverlayWindow");
        return false;
    }

    // Build OverlaySettings from app config (with type conversions)
    OverlaySettings overlaySettings;
    overlaySettings.enabled = config.overlay.enabled;
    overlaySettings.mode = config.overlay.mode;
    overlaySettings.position = config.overlay.position;  // Same type
    overlaySettings.monitor = config.overlay.monitor;    // Same type
    overlaySettings.format = config.overlay.format;
    overlaySettings.autoHide = config.overlay.autoHide;
    overlaySettings.autoHideDelayMs = config.overlay.autoHideDelayMs;
    
    // Watermark settings
    overlaySettings.watermarkFontSize = config.overlay.watermarkFontSize;
    overlaySettings.watermarkOpacity = config.overlay.watermarkOpacity;
    overlaySettings.watermarkShadow = config.overlay.watermarkShadow;
    overlaySettings.watermarkColor = config.overlay.watermarkColor;
    
    // Dodge settings
    overlaySettings.dodgeOnHover = config.overlay.dodgeOnHover;
    overlaySettings.dodgeProximity = config.overlay.dodgeProximity;
    
    // Style settings - BlurType maps to backdrop
    overlaySettings.style.backdrop = config.overlay.style.blur;
    overlaySettings.style.tintColor = config.overlay.style.tintColor;
    overlaySettings.style.tintOpacity = config.overlay.style.tintOpacity;
    overlaySettings.style.cornerRadius = config.overlay.style.cornerRadius;
    overlaySettings.style.padding = config.overlay.style.padding;
    overlaySettings.style.borderWidth = config.overlay.style.borderWidth;
    overlaySettings.style.borderColor = config.overlay.style.borderColor;
    
    // Text settings
    overlaySettings.text.fontFamily = config.overlay.text.fontFamily;
    overlaySettings.text.fontSize = config.overlay.text.fontSize;
    overlaySettings.text.fontWeight = config.overlay.text.fontWeight;
    overlaySettings.text.color = config.overlay.text.color;
    
    // Animation settings
    overlaySettings.animation.fadeInDurationMs = config.overlay.animation.fadeInDurationMs;
    overlaySettings.animation.fadeOutDurationMs = config.overlay.animation.fadeOutDurationMs;
    overlaySettings.animation.slideIn = config.overlay.animation.slideIn;
    overlaySettings.animation.slideDistance = config.overlay.animation.slideDistance;
    
    OverlayWindow::Instance().ApplySettings(overlaySettings);

    // Register for desktop change notifications
    VirtualDesktop::Instance().SetDesktopSwitchCallback(
        [](int index, const std::wstring& name) {
            App::Instance().OnDesktopSwitched(index, name);
        }
    );

    // For watermark mode, show immediately with current desktop info
    if (config.overlay.mode == OverlayMode::Watermark && config.overlay.enabled) {
        LOG_INFO("Watermark mode enabled, showing overlay on startup");
        DesktopInfo desktopInfo;
        if (VirtualDesktop::Instance().GetCurrentDesktop(desktopInfo)) {
            LOG_INFO("Current desktop: index=%d, name=%ws", desktopInfo.index, desktopInfo.name.c_str());
            OverlayWindow::Instance().Show(desktopInfo.index, desktopInfo.name);
        } else {
            // Fallback: show with desktop 1
            LOG_WARN("Failed to get current desktop, using fallback");
            OverlayWindow::Instance().Show(1, L"Desktop 1");
        }
    } else {
        LOG_INFO("Not showing watermark on startup: mode=%d, enabled=%d",
                 static_cast<int>(config.overlay.mode), config.overlay.enabled ? 1 : 0);
    }

    LOG_INFO("Overlay feature initialized (VirtualDesktop available: %s)",
             VirtualDesktop::Instance().IsAvailable() ? "true" : "false");
    return true;
}

void App::OnDesktopSwitched(int desktopIndex, const std::wstring& desktopName) {
    if (!m_overlayEnabled) {
        return;
    }

    LOG_DEBUG("Desktop switch event: %d (%ws)", desktopIndex, desktopName.c_str());
    OverlayWindow::Instance().Show(desktopIndex, desktopName);
}

void App::OnToggleOverlay() {
    if (!m_overlayEnabled) {
        return;
    }
    
    if (OverlayWindow::Instance().IsVisible()) {
        OverlayWindow::Instance().Hide();
        LOG_DEBUG("Overlay hidden via hotkey");
    } else {
        // Show overlay with current desktop info
        DesktopInfo desktopInfo;
        if (VirtualDesktop::Instance().GetCurrentDesktop(desktopInfo)) {
            OverlayWindow::Instance().Show(desktopInfo.index, desktopInfo.name);
        } else {
            OverlayWindow::Instance().Show(1, L"Desktop 1");
        }
        LOG_DEBUG("Overlay shown via hotkey");
    }
}

void App::OnDisplayChange() {
    LOG_INFO("Display configuration changed, refreshing monitors");
    Monitor::Instance().Refresh();
    
    // Notify overlay to reposition
    if (m_overlayEnabled) {
        OverlayWindow::Instance().OnDisplayChanged();
    }
}

void App::OnDpiChanged(HWND hwnd, UINT dpi, const RECT* suggested) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(suggested);
    
    LOG_INFO("DPI changed to %u", dpi);

    // Future: Update overlay sizing
    // if (m_overlayWindow) m_overlayWindow->OnDpiChanged(dpi);
}

void App::OnSettingsChanged() {
    LOG_INFO("Settings changed, applying from Config");
    
    const auto& config = Config::Instance().Get();
    
    LOG_INFO("Config values: overlay.position=%d, overlay.style.blur=%d",
             static_cast<int>(config.overlay.position),
             static_cast<int>(config.overlay.style.blur));

    // Update zoom config if enabled
    if (m_zoomEnabled) {
        ZoomSettings zoomSettings;
        zoomSettings.enabled = config.zoom.enabled;
        zoomSettings.modifierVirtualKey = ModifierKeyToVK(config.zoom.modifierKey);
        zoomSettings.zoomStep = config.zoom.zoomStep;
        zoomSettings.minZoom = config.zoom.minZoom;
        zoomSettings.maxZoom = config.zoom.maxZoom;
        zoomSettings.smoothing = config.zoom.smoothing;
        zoomSettings.smoothingFactor = config.zoom.smoothingFactor;
        zoomSettings.doubleTapToReset = config.zoom.doubleTapToReset;
        zoomSettings.doubleTapWindowMs = config.zoom.doubleTapWindowMs;
        
        ZoomController::Instance().ApplyConfig(zoomSettings);
        InputHandler::Instance().SetModifierKey(zoomSettings.modifierVirtualKey);
    }

    // Update overlay settings if enabled
    if (m_overlayEnabled) {
        OverlaySettings overlaySettings;
        overlaySettings.enabled = config.overlay.enabled;
        overlaySettings.mode = config.overlay.mode;
        overlaySettings.position = config.overlay.position;
        overlaySettings.monitor = config.overlay.monitor;
        overlaySettings.format = config.overlay.format;
        overlaySettings.autoHide = config.overlay.autoHide;
        overlaySettings.autoHideDelayMs = config.overlay.autoHideDelayMs;
        
        // Watermark settings
        overlaySettings.watermarkFontSize = config.overlay.watermarkFontSize;
        overlaySettings.watermarkOpacity = config.overlay.watermarkOpacity;
        overlaySettings.watermarkShadow = config.overlay.watermarkShadow;
        overlaySettings.watermarkColor = config.overlay.watermarkColor;
        
        // Dodge settings
        overlaySettings.dodgeOnHover = config.overlay.dodgeOnHover;
        overlaySettings.dodgeProximity = config.overlay.dodgeProximity;
        
        overlaySettings.style.backdrop = config.overlay.style.blur;
        overlaySettings.style.tintColor = config.overlay.style.tintColor;
        overlaySettings.style.tintOpacity = config.overlay.style.tintOpacity;
        overlaySettings.style.cornerRadius = config.overlay.style.cornerRadius;
        overlaySettings.style.padding = config.overlay.style.padding;
        overlaySettings.style.borderWidth = config.overlay.style.borderWidth;
        overlaySettings.style.borderColor = config.overlay.style.borderColor;
        
        overlaySettings.text.fontFamily = config.overlay.text.fontFamily;
        overlaySettings.text.fontSize = config.overlay.text.fontSize;
        overlaySettings.text.fontWeight = config.overlay.text.fontWeight;
        overlaySettings.text.color = config.overlay.text.color;
        
        overlaySettings.animation.fadeInDurationMs = config.overlay.animation.fadeInDurationMs;
        overlaySettings.animation.fadeOutDurationMs = config.overlay.animation.fadeOutDurationMs;
        overlaySettings.animation.slideIn = config.overlay.animation.slideIn;
        overlaySettings.animation.slideDistance = config.overlay.animation.slideDistance;
        
        OverlayWindow::Instance().ApplySettings(overlaySettings);
        LOG_INFO("Overlay settings applied");
        
        // In watermark mode, refresh with real desktop name (not preview text)
        if (config.overlay.mode == OverlayMode::Watermark && config.overlay.enabled) {
            DesktopInfo desktopInfo;
            if (VirtualDesktop::Instance().GetCurrentDesktop(desktopInfo)) {
                OverlayWindow::Instance().Show(desktopInfo.index, desktopInfo.name);
            }
        }
    }
}

void App::OnZoomIn() {
    if (m_zoomEnabled) {
        ZoomController::Instance().ZoomIn();
    }
}

void App::OnZoomOut() {
    if (m_zoomEnabled) {
        ZoomController::Instance().ZoomOut();
    }
}

void App::OnZoomReset() {
    if (m_zoomEnabled) {
        ZoomController::Instance().ResetZoom();
    }
}

void App::OnModifierDown() {
    if (m_zoomEnabled) {
        ZoomController::Instance().OnModifierPressed();
    }
}

void App::OnModifierUp() {
    if (m_zoomEnabled) {
        ZoomController::Instance().OnModifierReleased();
    }
}

void App::OnCursorMove(int x, int y) {
    if (m_zoomEnabled && ZoomController::Instance().IsZoomed()) {
        ZoomController::Instance().OnCursorMove(x, y);
    }
}

void App::OnZoomTimer() {
    if (!m_zoomEnabled) return;

    DWORD now = GetTickCount();
    float deltaMs = static_cast<float>(now - m_lastUpdateTime);
    m_lastUpdateTime = now;

    // Clamp delta to reasonable range (avoid huge jumps if app was paused)
    if (deltaMs > 100.0f) deltaMs = 100.0f;
    if (deltaMs < 1.0f) deltaMs = 1.0f;

    ZoomController::Instance().Update(deltaMs);

    // Update cursor tracking based on zoom state
    // This reduces message volume when not zoomed
    InputHandler::Instance().SetCursorTracking(ZoomController::Instance().IsZoomed());
}

bool App::InitSettings() {
    if (!SettingsWindow::Instance().Init(m_hInstance, m_hMainWnd)) {
        LOG_WARN("Failed to initialize settings window");
        return false;
    }

    // Set callback to reload settings when applied
    SettingsWindow::Instance().SetApplyCallback([this]() {
        OnSettingsChanged();
    });

    LOG_INFO("Settings window initialized");
    return true;
}

void App::OpenSettings() {
    SettingsWindow::Instance().Open();
}

bool App::InitTrayIcon() {
    if (!TrayIcon::Instance().Init(m_hInstance, m_hMainWnd)) {
        LOG_WARN("Failed to initialize tray icon");
        return false;
    }

    // Set callbacks
    TrayIcon::Instance().SetSettingsCallback([this]() {
        OpenSettings();
    });

    TrayIcon::Instance().SetAboutCallback([this]() {
        ShowAbout();
    });

    TrayIcon::Instance().SetExitCallback([this]() {
        PostMessageW(m_hMainWnd, WM_CLOSE, 0, 0);
    });

    // Show tray icon
    TrayIcon::Instance().Show();

    LOG_INFO("Tray icon initialized and shown");
    return true;
}

void App::ShowAbout() {
    MessageBoxW(
        m_hMainWnd,
        L"Virtual Overlay\n"
        L"Version 1.0.0\n\n"
        L"A Windows utility for virtual desktop overlay\n"
        L"and macOS-style screen zoom.\n\n"
        L"© 2026 Virtual Overlay Contributors",
        L"About Virtual Overlay",
        MB_OK | MB_ICONINFORMATION
    );
}

}  // namespace VirtualOverlay
