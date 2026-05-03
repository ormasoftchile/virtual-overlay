#pragma once

#include <windows.h>
#include <memory>
#include <string>

namespace VirtualOverlay {

// Forward declarations for future components
class TrayIcon;
class OverlayWindow;

constexpr UINT_PTR TIMER_DESKTOP_POLL = 1;
constexpr UINT TIMER_DESKTOP_POLL_MS = 150;  // Desktop switch detection

// Hotkey IDs
constexpr int HOTKEY_OVERLAY_TOGGLE = 1;

// Application lifecycle controller
class App {
public:
    static App& Instance();

    // Initialize all application components
    // Returns true if initialization succeeded
    bool Init(HINSTANCE hInstance, HWND hMainWnd);

    // Run the application (called after Init)
    // Note: Message loop is handled in main.cpp, this handles app-level events
    void Run();

    // Shutdown all application components
    void Shutdown();

    // Accessors
    HINSTANCE GetInstance() const { return m_hInstance; }
    HWND GetMainWindow() const { return m_hMainWnd; }
    bool IsRunning() const { return m_running; }

    // Event handlers (called from window procedure)
    void OnDisplayChange();
    void OnDpiChanged(HWND hwnd, UINT dpi, const RECT* suggested);
    void OnSettingsChanged();
    void OnDesktopPollTimer();

    // Overlay event handler
    void OnDesktopSwitched(int desktopIndex, const std::wstring& desktopName);
    void OnToggleOverlay();

    // Settings window
    void OpenSettings();

    // About dialog
    void ShowAbout();

private:
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool InitMonitors();
    bool InitOverlay();
    bool InitSettings();
    bool InitTrayIcon();

    HINSTANCE m_hInstance = nullptr;
    HWND m_hMainWnd = nullptr;
    bool m_running = false;
    bool m_initialized = false;
    bool m_overlayEnabled = false;
};

}  // namespace VirtualOverlay
