#pragma once

#include <windows.h>
#include <memory>
#include <string>

namespace VirtualOverlay {

// Forward declarations for future components
class TrayIcon;
class OverlayWindow;

// Timer ID for zoom update
constexpr UINT_PTR TIMER_ZOOM_UPDATE = 1;
constexpr UINT_PTR TIMER_DESKTOP_POLL = 2;
constexpr UINT TIMER_ZOOM_INTERVAL_MS = 16;  // ~60 FPS
constexpr UINT TIMER_DESKTOP_POLL_MS = 150;  // Desktop switch detection

// Hotkey IDs
constexpr int HOTKEY_OVERLAY_TOGGLE = 1;

// Custom window messages
constexpr UINT WM_USER_OVERLAY_TOGGLE = WM_USER + 120;

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
    
    // Zoom event handlers
    void OnZoomIn();
    void OnZoomOut();
    void OnZoomReset();
    void OnModifierDown();
    void OnModifierUp();
    void OnCursorMove(int x, int y);
    void OnZoomTimer();
    void OnDesktopPollTimer();  // Desktop switch detection
    
    // Overlay event handler
    void OnDesktopSwitched(int desktopIndex, const std::wstring& desktopName);
    void OnToggleOverlay();  // Hotkey handler
    
    // Settings window
    void OpenSettings();
    
    // About dialog
    void ShowAbout();

    // Feature accessors (for future use)
    // TrayIcon* GetTrayIcon() const { return m_trayIcon.get(); }
    // OverlayWindow* GetOverlayWindow() const { return m_overlayWindow.get(); }

private:
    App();
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool InitLogging();
    bool InitConfig();
    bool InitMonitors();
    bool InitZoom();
    bool InitOverlay();
    bool InitSettings();
    bool InitTrayIcon();
    // bool InitHotkeys();

    HINSTANCE m_hInstance = nullptr;
    HWND m_hMainWnd = nullptr;
    bool m_running = false;
    bool m_initialized = false;
    bool m_zoomEnabled = false;
    bool m_overlayEnabled = false;

    DWORD m_lastUpdateTime = 0;

    // Future component pointers
    // std::unique_ptr<TrayIcon> m_trayIcon;
    // std::unique_ptr<OverlayWindow> m_overlayWindow;
};

}  // namespace VirtualOverlay
