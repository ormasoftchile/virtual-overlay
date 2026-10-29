#include "App.h"
#include "utils/Logger.h"
#include "utils/Monitor.h"
#include "config/Config.h"
#include "overlay/OverlayWindow.h"
#include "overlay/OverlayConfig.h"
#include "desktop/VirtualDesktop.h"
#include "display/FlipManager.h"
#include "presenter/PresenterController.h"
#include "settings/SettingsWindow.h"
#include "tray/TrayIcon.h"
#include <algorithm>
#include <cwctype>

namespace VirtualOverlay {

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

    const auto& config = Config::Instance().Get();

    // Initialize overlay feature if enabled
    if (config.overlay.enabled) {
        if (InitOverlay()) {
            m_overlayEnabled = true;
        } else {
            LOG_WARN("Failed to initialize overlay feature");
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

    // Initialize per-monitor horizontal flip (teleprompter mirror)
    FlipManager::Instance().Init(m_hInstance, m_hMainWnd);

    // Initialize native speech-following Presenter
    PresenterController::Instance().Init(m_hInstance, m_hMainWnd);

    // Register flip toggle hotkey
    UINT flipMods = 0, flipVk = 0;
    if (ParseHotkeyString(config.flip.toggleHotkey, flipMods, flipVk)) {
        if (RegisterHotKey(m_hMainWnd, HOTKEY_FLIP_TOGGLE, flipMods | MOD_NOREPEAT, flipVk)) {
            LOG_INFO("Registered flip toggle hotkey: %ws", config.flip.toggleHotkey.c_str());
        } else {
            LOG_WARN("Failed to register flip toggle hotkey");
        }
    }

    // Register presenter toggle hotkey
    UINT presMods = 0, presVk = 0;
    if (ParseHotkeyString(config.presenter.toggleHotkey, presMods, presVk)) {
        if (RegisterHotKey(m_hMainWnd, HOTKEY_PRESENTER_TOGGLE, presMods | MOD_NOREPEAT, presVk)) {
            LOG_INFO("Registered presenter toggle hotkey: %ws", config.presenter.toggleHotkey.c_str());
        } else {
            LOG_WARN("Failed to register presenter toggle hotkey");
        }
    }

    // Register settings hotkey
    UINT settingsMods = 0, settingsVk = 0;
    if (ParseHotkeyString(config.general.settingsHotkey, settingsMods, settingsVk)) {
        if (RegisterHotKey(m_hMainWnd, HOTKEY_SETTINGS, settingsMods | MOD_NOREPEAT, settingsVk)) {
            LOG_INFO("Registered settings hotkey: %ws", config.general.settingsHotkey.c_str());
        } else {
            LOG_WARN("Failed to register settings hotkey");
        }
    }

    m_initialized = true;
    m_running = true;

    LOG_INFO("Application initialized successfully");
    LOG_INFO("Detected %zu monitor(s)", Monitor::Instance().GetCount());
    LOG_INFO("Overlay enabled: %s", m_overlayEnabled ? "true" : "false");

    return true;
}

void App::Run() {
    if (!m_initialized) {
        LOG_ERROR("App::Run called before initialization");
        return;
    }

    LOG_DEBUG("Application running");
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
        UnregisterHotKey(m_hMainWnd, HOTKEY_FLIP_TOGGLE);
        UnregisterHotKey(m_hMainWnd, HOTKEY_PRESENTER_TOGGLE);
        UnregisterHotKey(m_hMainWnd, HOTKEY_SETTINGS);
    }

    if (m_hMainWnd) {
        KillTimer(m_hMainWnd, TIMER_DESKTOP_POLL);
    }

    // Shutdown Presenter
    PresenterController::Instance().Shutdown();

    // Shutdown flip (teleprompter mirror)
    FlipManager::Instance().Shutdown();

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

    m_initialized = false;
    LOG_INFO("Application shutdown complete");
}

bool App::InitMonitors() {
    auto& monitor = Monitor::Instance();
    
    if (monitor.GetCount() == 0) {
        LOG_WARN("No monitors detected");
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

bool App::InitOverlay() {
    const auto& config = Config::Instance().Get();
    
    LOG_INFO("InitOverlay: config.overlay.position=%d", static_cast<int>(config.overlay.position));

    if (!VirtualDesktop::Instance().Init()) {
        LOG_WARN("Failed to initialize VirtualDesktop - overlay will be limited");
    }

    if (!OverlayWindow::Instance().Init(m_hInstance)) {
        LOG_ERROR("Failed to initialize OverlayWindow");
        return false;
    }

    OverlaySettings overlaySettings;
    overlaySettings.enabled = config.overlay.enabled;
    overlaySettings.position = config.overlay.position;
    overlaySettings.monitor = config.overlay.monitor;
    overlaySettings.format = config.overlay.format;
    
    overlaySettings.watermarkFontSize = config.overlay.watermarkFontSize;
    overlaySettings.watermarkOpacity = config.overlay.watermarkOpacity;
    overlaySettings.watermarkShadow = config.overlay.watermarkShadow;
    overlaySettings.watermarkColor = config.overlay.watermarkColor;
    
    overlaySettings.dodgeOnHover = config.overlay.dodgeOnHover;
    overlaySettings.dodgeProximity = config.overlay.dodgeProximity;
    
    overlaySettings.style.padding = config.overlay.style.padding;
    
    overlaySettings.text.fontFamily = config.overlay.text.fontFamily;
    overlaySettings.text.fontWeight = config.overlay.text.fontWeight;
    
    OverlayWindow::Instance().ApplySettings(overlaySettings);

    VirtualDesktop::Instance().SetDesktopSwitchCallback(
        [](int index, const std::wstring& name) {
            App::Instance().OnDesktopSwitched(index, name);
        }
    );
    
    SetTimer(m_hMainWnd, TIMER_DESKTOP_POLL, TIMER_DESKTOP_POLL_MS, nullptr);
    LOG_INFO("Started desktop polling timer");

    if (config.overlay.enabled) {
        LOG_INFO("Watermark enabled, showing overlay on startup");
        DesktopInfo desktopInfo;
        if (VirtualDesktop::Instance().GetCurrentDesktop(desktopInfo)) {
            LOG_INFO("Current desktop: index=%d, name=%ws", desktopInfo.index, desktopInfo.name.c_str());
            OverlayWindow::Instance().Show(desktopInfo.index, desktopInfo.name);
        } else {
            LOG_WARN("Failed to get current desktop, using fallback");
            OverlayWindow::Instance().Show(1, L"Desktop 1");
        }
    }

    LOG_INFO("Overlay feature initialized (VirtualDesktop available: %s)",
             VirtualDesktop::Instance().IsAvailable() ? "true" : "false");
    return true;
}

void App::OnDesktopSwitched(int desktopIndex, const std::wstring& desktopName) {
    if (m_overlayEnabled) {
        LOG_DEBUG("Desktop switch event: %d (%ws)", desktopIndex, desktopName.c_str());
        OverlayWindow::Instance().Show(desktopIndex, desktopName);
    }

    // Move Presenter window to current virtual desktop if open
    if (PresenterController::Instance().IsOpen()) {
        PresenterController::Instance().OnDisplayChanged();
    }
}

void App::OnToggleFlip() {
    FlipManager::Instance().ToggleUnderCursor();
}

void App::OnTogglePresenter() {
    PresenterController::Instance().Toggle();
}

void App::OnToggleOverlay() {
    if (!m_overlayEnabled) {
        return;
    }
    
    if (OverlayWindow::Instance().IsVisible()) {
        OverlayWindow::Instance().Hide();
        LOG_DEBUG("Overlay hidden via hotkey");
    } else {
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
    
    if (m_overlayEnabled) {
        OverlayWindow::Instance().OnDisplayChanged();
    }

    FlipManager::Instance().OnDisplayChanged();
    PresenterController::Instance().OnDisplayChanged();
}

void App::OnDpiChanged(HWND hwnd, UINT dpi, const RECT* suggested) {
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(suggested);
    
    LOG_INFO("DPI changed to %u", dpi);
    OnDisplayChange();
}

void App::OnSettingsChanged() {
    LOG_INFO("Settings changed, applying from Config");
    
    const auto& config = Config::Instance().Get();

    if (m_overlayEnabled) {
        OverlaySettings overlaySettings;
        overlaySettings.enabled = config.overlay.enabled;
        overlaySettings.position = config.overlay.position;
        overlaySettings.monitor = config.overlay.monitor;
        overlaySettings.format = config.overlay.format;
        
        overlaySettings.watermarkFontSize = config.overlay.watermarkFontSize;
        overlaySettings.watermarkOpacity = config.overlay.watermarkOpacity;
        overlaySettings.watermarkShadow = config.overlay.watermarkShadow;
        overlaySettings.watermarkColor = config.overlay.watermarkColor;
        
        overlaySettings.dodgeOnHover = config.overlay.dodgeOnHover;
        overlaySettings.dodgeProximity = config.overlay.dodgeProximity;
        
        overlaySettings.style.padding = config.overlay.style.padding;
        
        overlaySettings.text.fontFamily = config.overlay.text.fontFamily;
        overlaySettings.text.fontWeight = config.overlay.text.fontWeight;
        
        OverlayWindow::Instance().ApplySettings(overlaySettings);
        
        if (config.overlay.enabled) {
            DesktopInfo desktopInfo;
            if (VirtualDesktop::Instance().GetCurrentDesktop(desktopInfo)) {
                OverlayWindow::Instance().Show(desktopInfo.index, desktopInfo.name);
            }
        }
    }

    // Re-apply flip and presenter settings
    FlipManager::Instance().ApplySettings();
    PresenterController::Instance().ApplySettings();

    // Re-register hotkeys
    if (m_hMainWnd) {
        UnregisterHotKey(m_hMainWnd, HOTKEY_OVERLAY_TOGGLE);
        UINT overlayMods = 0, overlayVk = 0;
        if (ParseHotkeyString(config.general.overlayToggleHotkey, overlayMods, overlayVk)) {
            RegisterHotKey(m_hMainWnd, HOTKEY_OVERLAY_TOGGLE, overlayMods | MOD_NOREPEAT, overlayVk);
        }

        UnregisterHotKey(m_hMainWnd, HOTKEY_FLIP_TOGGLE);
        UINT flipMods = 0, flipVk = 0;
        if (ParseHotkeyString(config.flip.toggleHotkey, flipMods, flipVk)) {
            RegisterHotKey(m_hMainWnd, HOTKEY_FLIP_TOGGLE, flipMods | MOD_NOREPEAT, flipVk);
        }

        UnregisterHotKey(m_hMainWnd, HOTKEY_PRESENTER_TOGGLE);
        UINT presMods = 0, presVk = 0;
        if (ParseHotkeyString(config.presenter.toggleHotkey, presMods, presVk)) {
            RegisterHotKey(m_hMainWnd, HOTKEY_PRESENTER_TOGGLE, presMods | MOD_NOREPEAT, presVk);
        }

        UnregisterHotKey(m_hMainWnd, HOTKEY_SETTINGS);
        UINT settingsMods = 0, settingsVk = 0;
        if (ParseHotkeyString(config.general.settingsHotkey, settingsMods, settingsVk)) {
            RegisterHotKey(m_hMainWnd, HOTKEY_SETTINGS, settingsMods | MOD_NOREPEAT, settingsVk);
        }
    }
}

void App::OnDesktopPollTimer() {
    if (!m_overlayEnabled) return;
    
    VirtualDesktop::Instance().CheckDesktopChange();

    static unsigned int s_posCheckTick = 0;
    if (++s_posCheckTick % 4 == 0) {
        OverlayWindow::Instance().CheckPosition();
    }
}

bool App::InitSettings() {
    if (!SettingsWindow::Instance().Init(m_hInstance, m_hMainWnd)) {
        LOG_WARN("Failed to initialize settings window");
        return false;
    }

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

    TrayIcon::Instance().SetSettingsCallback([this]() {
        OpenSettings();
    });

    TrayIcon::Instance().SetAboutCallback([this]() {
        ShowAbout();
    });

    TrayIcon::Instance().SetExitCallback([this]() {
        PostMessageW(m_hMainWnd, WM_CLOSE, 0, 0);
    });

    TrayIcon::Instance().SetFlipToggleCallback([](const std::wstring& deviceName) {
        FlipManager::Instance().Toggle(deviceName);
    });

    TrayIcon::Instance().SetFlipStateQuery([](const std::wstring& deviceName) {
        return FlipManager::Instance().IsFlipped(deviceName);
    });

    LOG_INFO("Tray icon initialized; show deferred to message loop");
    return true;
}

void App::ShowAbout() {
    MessageBoxW(
        m_hMainWnd,
        L"Virtual Overlay\n"
        L"Version 1.2.0\n\n"
        L"A Windows utility for virtual desktop overlay,\n"
        L"teleprompter screen flipping, and offline speech-following presenter.\n\n"
        L"Speech alignment adapted from TalkPrompter (\u00A9 2026 Sven Bosau, MIT).\n"
        L"\u00A9 2026 Virtual Overlay Contributors",
        L"About Virtual Overlay",
        MB_OK | MB_ICONINFORMATION
    );
}

}  // namespace VirtualOverlay
