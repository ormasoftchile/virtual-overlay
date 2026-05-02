#pragma once

#include <windows.h>
#include <shellapi.h>
#include <functional>

namespace VirtualOverlay {

// Custom messages for tray icon lifecycle and callbacks
constexpr UINT WM_APP_INIT_TRAY = WM_APP + 1;
constexpr UINT WM_TRAYICON = WM_USER + 300;
constexpr UINT TIMER_TRAY_RETRY = 200;

// Menu item IDs
constexpr UINT IDM_TRAY_SETTINGS = 1001;
constexpr UINT IDM_TRAY_ABOUT = 1002;
constexpr UINT IDM_TRAY_EXIT = 1003;
constexpr UINT IDM_TRAY_AUTOSTART = 1004;

// Tray icon manager
class TrayIcon {
public:
    static TrayIcon& Instance();

    // Lifecycle
    bool Init(HINSTANCE hInstance, HWND hParentWnd);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Show/hide tray icon
    void Show();
    void Hide();
    void Restore();
    void OnRetryTimer();
    bool IsVisible() const { return m_visible; }

    // Update tooltip
    void SetTooltip(const wchar_t* tooltip);

    // Handle tray message (called from WndProc)
    void HandleMessage(WPARAM wParam, LPARAM lParam);

    // Callbacks
    using MenuCallback = std::function<void()>;
    void SetSettingsCallback(MenuCallback callback) { m_onSettings = callback; }
    void SetAboutCallback(MenuCallback callback) { m_onAbout = callback; }
    void SetExitCallback(MenuCallback callback) { m_onExit = callback; }

private:
    TrayIcon();
    ~TrayIcon();
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    // Show context menu
    bool AddIcon();
    void ShowContextMenu();

    // Auto-start helpers
    bool IsAutoStartEnabled();
    void SetAutoStart(bool enable);

    // Members
    HINSTANCE m_hInstance = nullptr;
    HWND m_hParentWnd = nullptr;
    NOTIFYICONDATAW m_nid = {};
    HMENU m_hMenu = nullptr;
    bool m_initialized = false;
    bool m_visible = false;
    static constexpr int MAX_TRAY_RETRIES = 10;
    static constexpr UINT TRAY_RETRY_INTERVAL_MS = 1000;
    int m_addRetryCount = 0;

    // Callbacks
    MenuCallback m_onSettings;
    MenuCallback m_onAbout;
    MenuCallback m_onExit;
};

}  // namespace VirtualOverlay
