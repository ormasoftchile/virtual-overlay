#pragma once

#include <windows.h>
#include <shellapi.h>
#include <functional>

namespace VirtualOverlay {

// Custom message for tray icon
constexpr UINT WM_TRAYICON = WM_USER + 300;

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

    // Callbacks
    MenuCallback m_onSettings;
    MenuCallback m_onAbout;
    MenuCallback m_onExit;
};

}  // namespace VirtualOverlay
